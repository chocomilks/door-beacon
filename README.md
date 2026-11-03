# 현관 비콘 (door-beacon)

갤럭시 폰이 고정된 iBeacon 신호만 약 1초 간격으로 내보내는 초간단 앱입니다. ESPresense로 세대현관 자동화를 할 때 씁니다.
스캔·위치·네트워크를 쓰지 않아서 배터리 소모가 아주 적습니다.

## 설치

폰 브라우저에서 아래 주소를 열어 APK를 받습니다.
`https://github.com/chocomilks/door-beacon/releases/latest/download/door-beacon.apk`

1. "출처를 알 수 없는 앱" 설치를 허용하고 설치합니다.
2. 앱을 열고 **비콘 켜기**를 누른 뒤 '근처 기기'와 알림 권한을 허용합니다.
3. **절전 예외로 설정하기**를 누릅니다. 갤럭시는 추가로 설정 → 애플리케이션 → 현관 비콘 → 배터리 → **제한 없음**으로 바꿉니다.

## ESPresense / Home Assistant

- 앱 화면의 ID(`iBeacon:<uuid>-100-1`)를 복사합니다.
- ESPresense 노드 설정의 **Known BLE identifiers**에 넣거나, ESPresense-companion에서 기기로 등록합니다.
- 현관 근처 노드에서 폰을 1m 거리에 두고 RSSI를 보정합니다(Ref RSSI).
- MQTT room 센서(`sensor.<이름>`)의 방/거리를 조건으로 자동화를 만듭니다.

송신 세기는 앱에서 약/보통/강 중 고를 수 있습니다. 바꾸면 ESPresense의 보정값도 다시 맞춰 주세요.

## 보안

iBeacon 신호는 누구나 똑같이 흉내 낼 수 있습니다. 문 잠금 해제에 쓴다면 집 와이파이 연결, 직전 외출 상태 같은 조건을 하나 더 거세요.

## 빌드

`main`에 푸시하면 GitHub Actions가 APK를 빌드해 Releases에 올립니다. 서명키는 저장소 Secrets(`KEYSTORE_BASE64`, `KEYSTORE_PASSWORD`, `KEY_ALIAS`, `KEY_PASSWORD`)에서 읽습니다. 키 파일은 저장소에 절대 올리지 마세요. 키가 같아야 업데이트할 때 지우고 다시 깔지 않아도 됩니다.
