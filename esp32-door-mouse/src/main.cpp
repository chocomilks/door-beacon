/*
 * 현관 도어 트리거 테스트 — ESP32(WROOM) BLE 마우스 (NimBLE 버전)
 * ------------------------------------------------------------------
 * 이전 라이브러리(T-vK)는 본딩/재연결이 안 돼서 NimBLE로 교체.
 * NimBLE는 본딩 키를 NVS(플래시)에 저장하므로 폰이 다시 다가오면 재연결된다.
 *
 * 목표: 폰이 멀어졌다가 현관으로 다시 올 때 "재연결까지 몇 초" 측정.
 *
 * 테스트 순서:
 *   1) 폰 블루투스에서 'Door Mouse' 페어링(한 번)
 *   2) [최초 연결됨] 뜨면 폰 들고 멀리 -> [끊김] 확인
 *   3) 폰 화면 끄고 1~2분 뒤(절전 진입) 현관으로 다가오기
 *   4) [재연결] 걸린 시간 기록 (여러 번 반복)
 *
 * 판정:
 *   - 2~3초 이내로 꾸준  -> 문 트리거로 쓸만함
 *   - 10초 이상/들쭉날쭉 -> 문 트리거엔 무리
 */

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

// --- HID 마우스 리포트 맵 (버튼3 + X/Y/휠) ---
static const uint8_t MOUSE_REPORT_MAP[] = {
  0x05, 0x01,  // Usage Page (Generic Desktop)
  0x09, 0x02,  // Usage (Mouse)
  0xA1, 0x01,  // Collection (Application)
  0x09, 0x01,  //   Usage (Pointer)
  0xA1, 0x00,  //   Collection (Physical)
  0x05, 0x09,  //     Usage Page (Buttons)
  0x19, 0x01,  //     Usage Minimum (1)
  0x29, 0x03,  //     Usage Maximum (3)
  0x15, 0x00,  //     Logical Minimum (0)
  0x25, 0x01,  //     Logical Maximum (1)
  0x95, 0x03,  //     Report Count (3)
  0x75, 0x01,  //     Report Size (1)
  0x81, 0x02,  //     Input (Data,Var,Abs)
  0x95, 0x01,  //     Report Count (1)
  0x75, 0x05,  //     Report Size (5)
  0x81, 0x03,  //     Input (Const) 패딩
  0x05, 0x01,  //     Usage Page (Generic Desktop)
  0x09, 0x30,  //     Usage (X)
  0x09, 0x31,  //     Usage (Y)
  0x09, 0x38,  //     Usage (Wheel)
  0x15, 0x81,  //     Logical Min (-127)
  0x25, 0x7F,  //     Logical Max (127)
  0x75, 0x08,  //     Report Size (8)
  0x95, 0x03,  //     Report Count (3)
  0x81, 0x06,  //     Input (Data,Var,Rel)
  0xC0,        //   End Collection
  0xC0         // End Collection
};

static NimBLEHIDDevice* hid = nullptr;
static bool everDisconnected = false;
static unsigned long lastDisconnectMs = 0;

class SrvCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, ble_gap_conn_desc* desc) override {
    unsigned long t = millis();
    if (everDisconnected) {
      unsigned long dt = t - lastDisconnectMs;
      Serial.printf(">>> [재연결]  걸린 시간 = %lu.%03lu 초\n", dt / 1000, dt % 1000);
    } else {
      Serial.println(">>> [최초 연결됨]  이제 폰을 멀리 가져가 보세요");
    }
  }
  void onDisconnect(NimBLEServer* s) override {
    lastDisconnectMs = millis();
    everDisconnected = true;
    Serial.println(">>> [끊김]  폰 화면 끄고, 잠시 뒤 현관으로 다가와 재연결 시간 측정");
    NimBLEDevice::startAdvertising();  // 다시 광고해서 재연결을 받음
  }
};

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println();
  Serial.println("=== 현관 BLE 마우스 (NimBLE, 재연결 시간 측정) ===");
  Serial.println(" 1) 폰에서 'Door Mouse' 페어링(한 번)");
  Serial.println(" 2) [최초 연결됨] 뜨면 폰 들고 멀리 -> [끊김]");
  Serial.println(" 3) 폰 화면 끄고 1~2분 뒤 현관으로 다가오기");
  Serial.println(" 4) [재연결] 걸린 시간 기록 (여러 번)");
  Serial.println();

  NimBLEDevice::init("Door Mouse");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  // 본딩(키를 NVS에 저장) + MITM 없음 + Secure Connections
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);  // Just Works

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new SrvCB());

  hid = new NimBLEHIDDevice(server);
  hid->inputReport(1);                 // 마우스 입력 리포트
  hid->pnp(0x02, 0xE502, 0xA111, 0x0210);
  hid->hidInfo(0x00, 0x01);
  hid->reportMap((uint8_t*)MOUSE_REPORT_MAP, sizeof(MOUSE_REPORT_MAP));
  hid->startServices();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(0x03C2);          // HID 마우스
  adv->addServiceUUID(hid->hidService()->getUUID());
  adv->setScanResponse(true);
  NimBLEDevice::startAdvertising();

  Serial.println("준비됨. 폰에서 'Door Mouse'를 페어링하세요.");
}

void loop() {
  delay(1000);
}
