/*
 * 현관 도어 트리거 테스트 — ESP32(WROOM) BLE 마우스
 * ------------------------------------------------------
 * 1단계 목표: 폰이 멀어졌다가 현관으로 다시 다가올 때
 *            "재연결까지 몇 초 걸리는지"만 시리얼로 측정한다.
 *            (이 숫자가 작아야 문 트리거로 쓸 수 있다)
 *
 * 테스트 순서:
 *   1) 업로드 후 시리얼 모니터(115200)를 연다.
 *   2) 폰 블루투스에서 'Door Mouse'를 한 번 페어링한다.
 *   3) [최초 연결됨]이 뜨면, 폰을 들고 집 밖(또는 먼 방)으로 가서 [끊김]을 확인한다.
 *   4) 폰 화면을 끄고 주머니에 넣은 채로 1~2분 둔 뒤(절전 진입), 현관으로 다가온다.
 *   5) 출력되는 [재연결] '걸린 시간'을 기록한다. 여러 번 반복해 평균을 본다.
 *
 * 판정 기준(대략):
 *   - 2~3초 이내로 꾸준  -> 문 트리거로 쓸만함 (2단계로 진행)
 *   - 10초 이상/들쭉날쭉 -> 문 트리거엔 무리 (집/외출 신호로만 활용 고려)
 */

#include <Arduino.h>
#include <BleMouse.h>

// 광고 이름 "Door Mouse" — 폰 블루투스 목록에 이 이름으로 뜬다
BleMouse bleMouse("Door Mouse", "DIY-HA", 100);

bool wasConnected = false;
bool everDisconnected = false;
unsigned long lastDisconnectMs = 0;

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println();
  Serial.println("=== 현관 BLE 마우스 (재연결 시간 측정) ===");
  Serial.println(" 1) 폰에서 'Door Mouse' 페어링(한 번)");
  Serial.println(" 2) [최초 연결됨] 뜨면 폰 들고 멀리 -> [끊김] 확인");
  Serial.println(" 3) 폰 화면 끄고 1~2분 뒤 현관으로 다가오기");
  Serial.println(" 4) [재연결] 걸린 시간 기록 (여러 번 반복)");
  Serial.println();
  bleMouse.begin();
}

void loop() {
  bool nowConnected = bleMouse.isConnected();

  // 끊겼다가 다시 붙은 순간
  if (nowConnected && !wasConnected) {
    unsigned long t = millis();
    if (everDisconnected) {
      unsigned long dt = t - lastDisconnectMs;
      Serial.printf(">>> [재연결]  걸린 시간 = %lu.%03lu 초\n", dt / 1000, dt % 1000);
    } else {
      Serial.println(">>> [최초 연결됨]  이제 폰을 멀리 가져가 보세요");
    }
  }

  // 연결이 끊긴 순간
  if (!nowConnected && wasConnected) {
    lastDisconnectMs = millis();
    everDisconnected = true;
    Serial.println(">>> [끊김]  폰 화면 끄고, 잠시 뒤 현관으로 다가와 재연결 시간 측정");
  }

  wasConnected = nowConnected;
  delay(20);
}
