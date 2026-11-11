/*
 * 현관 도어 트리거 — ESP32(WROOM) BLE 마우스 + WiFi/MQTT  (2단계)
 * ------------------------------------------------------------------
 * 동작:
 *   - NimBLE HID 마우스로 폰과 본딩(1단계와 동일) -> 폰이 배경에서 자동 재연결
 *   - 연결되면 WiFi/MQTT로 HA에 보고: 재실(연결) / RSSI / away_s(끊겨있던 초)
 *   - HA 자동화가 "도착(away>60초 뒤 재연결) + RSSI가 문 앞 수준"일 때 문 열기
 *
 * 처음 부팅:
 *   - 저장된 WiFi가 없으면 'Door-Mouse-setup' AP가 뜸.
 *     폰으로 접속 -> 집 WiFi + MQTT 브로커(IP/포트/계정) 입력 -> 저장.
 *   - 코드에 비번을 안 박으므로 공개 저장소에도 안전.
 *
 * HA에 자동 생성되는 엔티티(MQTT 디스커버리):
 *   - binary_sensor.doormouse_presence  (연결=켜짐, 속성에 rssi/away_s)
 *   - sensor.doormouse_rssi             (연결 중 RSSI, dBm) -> 문턱값 캘리브레이션용
 *
 * RSSI는 '연결된 링크'의 세기라 폰이 광고를 안 해도 읽혀요(삼성 OK).
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

// NimBLE 호스트 함수(라이브러리에 포함됨)를 직접 선언 — 연결 링크 RSSI 읽기.
// (host/ble_gap.h 헤더 경로가 스케치에서 안 잡혀서 프로토타입만 선언)
extern "C" int ble_gap_conn_rssi(uint16_t conn_handle, int8_t *out_rssi);

// ---------------- 설정/전역 ----------------
Preferences prefs;
WiFiClient   net;
PubSubClient mqtt(net);

String   mqttHost, mqttUser, mqttPass;
uint16_t mqttPort = 1883;

static const char* ST_TOPIC = "doormouse/state";   // 상태(JSON)
static const char* AV_TOPIC = "doormouse/status";  // 가용성(LWT)

volatile bool bleConnected  = false;
volatile bool stateDirty    = false;
uint16_t      connHandle    = 0;
bool          everDisconnected = false;
unsigned long lastConnectMs  = 0;
unsigned long lastDisconnectMs = 0;
long          awaySeconds = -1;   // 직전에 끊겨 있던 시간(초). 첫 연결은 -1
int           curRssi = 0;

// ---------------- HID 마우스 리포트 맵 ----------------
static const uint8_t MOUSE_REPORT_MAP[] = {
  0x05,0x01, 0x09,0x02, 0xA1,0x01, 0x09,0x01, 0xA1,0x00,
  0x05,0x09, 0x19,0x01, 0x29,0x03, 0x15,0x00, 0x25,0x01,
  0x95,0x03, 0x75,0x01, 0x81,0x02, 0x95,0x01, 0x75,0x05, 0x81,0x03,
  0x05,0x01, 0x09,0x30, 0x09,0x31, 0x09,0x38, 0x15,0x81, 0x25,0x7F,
  0x75,0x08, 0x95,0x03, 0x81,0x06, 0xC0, 0xC0
};

// ---------------- MQTT 발행 ----------------
void publishState() {
  char buf[128];
  snprintf(buf, sizeof(buf),
    "{\"connected\":%s,\"rssi\":%d,\"away_s\":%ld}",
    bleConnected ? "true" : "false",
    bleConnected ? curRssi : 0,
    awaySeconds);
  if (mqtt.connected()) mqtt.publish(ST_TOPIC, buf, true);
  Serial.println(buf);
}

void publishDiscovery() {
  char c[700];
  // 재실 binary_sensor (rssi/away_s는 속성으로)
  snprintf(c, sizeof(c),
R"({"name":"현관 도어마우스 재실","obj_id":"doormouse_presence","uniq_id":"doormouse_presence","dev_cla":"presence","stat_t":"%s","val_tpl":"{{ 'ON' if value_json.connected else 'OFF' }}","json_attr_t":"%s","avty_t":"%s","dev":{"ids":["doormouse"],"name":"현관 도어마우스","mdl":"ESP32 BLE Mouse","mf":"DIY-HA"}})",
    ST_TOPIC, ST_TOPIC, AV_TOPIC);
  mqtt.publish("homeassistant/binary_sensor/doormouse/presence/config", c, true);

  // RSSI sensor (캘리브레이션용)
  snprintf(c, sizeof(c),
R"({"name":"현관 도어마우스 RSSI","obj_id":"doormouse_rssi","uniq_id":"doormouse_rssi","dev_cla":"signal_strength","unit_of_meas":"dBm","stat_cla":"measurement","stat_t":"%s","val_tpl":"{{ value_json.rssi }}","avty_t":"%s","dev":{"ids":["doormouse"]}})",
    ST_TOPIC, AV_TOPIC);
  mqtt.publish("homeassistant/sensor/doormouse/rssi/config", c, true);
}

// ---------------- BLE 콜백 ----------------
class SrvCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, ble_gap_conn_desc* desc) override {
    connHandle = desc->conn_handle;
    bleConnected = true;
    lastConnectMs = millis();
    awaySeconds = everDisconnected ? (long)((lastConnectMs - lastDisconnectMs) / 1000) : -1;
    stateDirty = true;
    Serial.printf(">>> [연결] away_s=%ld\n", awaySeconds);
  }
  void onDisconnect(NimBLEServer* s) override {
    bleConnected = false;
    lastDisconnectMs = millis();
    everDisconnected = true;
    stateDirty = true;
    Serial.println(">>> [끊김]");
    NimBLEDevice::startAdvertising();  // 재연결 받기
  }
};

// ---------------- WiFi/MQTT ----------------
void setupWiFi() {
  prefs.begin("doormouse", false);
  mqttHost = prefs.getString("host", "");
  mqttPort = prefs.getUShort("port", 1883);
  mqttUser = prefs.getString("user", "");
  mqttPass = prefs.getString("pass", "");

  WiFiManager wm;
  char portbuf[8]; snprintf(portbuf, sizeof(portbuf), "%u", mqttPort);
  WiFiManagerParameter p_host("host", "MQTT 브로커 IP", mqttHost.c_str(), 40);
  WiFiManagerParameter p_port("port", "MQTT 포트", portbuf, 6);
  WiFiManagerParameter p_user("user", "MQTT 사용자(없으면 비움)", mqttUser.c_str(), 40);
  WiFiManagerParameter p_pass("pass", "MQTT 비밀번호(없으면 비움)", mqttPass.c_str(), 40);
  wm.addParameter(&p_host);
  wm.addParameter(&p_port);
  wm.addParameter(&p_user);
  wm.addParameter(&p_pass);
  wm.setConfigPortalTimeout(300);

  bool ok = wm.autoConnect("Door-Mouse-setup");

  // 포털에서 입력했거나(또는 기존값 그대로) 저장
  mqttHost = p_host.getValue();
  mqttPort = atoi(p_port.getValue()); if (mqttPort == 0) mqttPort = 1883;
  mqttUser = p_user.getValue();
  mqttPass = p_pass.getValue();
  prefs.putString("host", mqttHost);
  prefs.putUShort("port", mqttPort);
  prefs.putString("user", mqttUser);
  prefs.putString("pass", mqttPass);

  Serial.printf("WiFi %s / MQTT %s:%u\n", ok ? "연결됨" : "실패(나중 재시도)",
                mqttHost.c_str(), mqttPort);
}

void mqttEnsure() {
  if (mqtt.connected() || mqttHost.length() == 0) return;
  mqtt.setServer(mqttHost.c_str(), mqttPort);
  mqtt.setBufferSize(1024);  // 디스커버리 페이로드가 256B 기본보다 큼
  String cid = "doormouse-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  bool ok = mqtt.connect(cid.c_str(),
                         mqttUser.length() ? mqttUser.c_str() : nullptr,
                         mqttPass.length() ? mqttPass.c_str() : nullptr,
                         AV_TOPIC, 0, true, "offline");
  if (ok) {
    mqtt.publish(AV_TOPIC, "online", true);
    publishDiscovery();
    publishState();
    Serial.println("MQTT 연결됨, 디스커버리 발행");
  }
}

// ---------------- setup / loop ----------------
void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println("\n=== 현관 도어마우스 (2단계: BLE + WiFi/MQTT) ===");

  setupWiFi();

  // BLE HID 마우스 (본딩 저장 -> 재연결)
  NimBLEDevice::init("Door Mouse");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);        // 집 전체 덮게 최대 출력
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new SrvCB());

  NimBLEHIDDevice* hid = new NimBLEHIDDevice(server);
  hid->inputReport(1);
  hid->pnp(0x02, 0xE502, 0xA111, 0x0210);
  hid->hidInfo(0x00, 0x01);
  hid->reportMap((uint8_t*)MOUSE_REPORT_MAP, sizeof(MOUSE_REPORT_MAP));
  hid->startServices();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(0x03C2);
  adv->addServiceUUID(hid->hidService()->getUUID());
  adv->setScanResponse(true);
  NimBLEDevice::startAdvertising();

  Serial.println("준비됨. 폰에서 'Door Mouse' 페어링하세요.");
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    mqttEnsure();
    mqtt.loop();
  }

  // 연결 중이면 링크 RSSI 갱신
  if (bleConnected) {
    int8_t r = 0;
    if (ble_gap_conn_rssi(connHandle, &r) == 0) curRssi = r;
  }

  // 이벤트 발생 시 즉시, 아니면 1.5초마다 발행
  static unsigned long lastPub = 0;
  if (stateDirty || millis() - lastPub > 1500) {
    publishState();
    stateDirty = false;
    lastPub = millis();
  }

  delay(20);
}
