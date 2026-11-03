package com.home.doorbeacon

import android.annotation.SuppressLint
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.bluetooth.le.AdvertiseCallback
import android.bluetooth.le.AdvertiseData
import android.bluetooth.le.AdvertiseSettings
import android.bluetooth.le.BluetoothLeAdvertiser
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder

/**
 * iBeacon 신호만 계속 내보내는 포그라운드 서비스.
 * 스캔·연결·위치 사용 없음. 블루투스가 꺼졌다 켜지면 자동으로 다시 시작합니다.
 */
class BeaconService : Service() {

    companion object {
        private const val CHANNEL_ID = "beacon"
        private const val NOTIF_ID = 1
        const val ACTION_RESTART = "com.home.doorbeacon.RESTART"

        fun start(ctx: Context) {
            ctx.startForegroundService(Intent(ctx, BeaconService::class.java))
        }

        fun restart(ctx: Context) {
            ctx.startForegroundService(Intent(ctx, BeaconService::class.java).setAction(ACTION_RESTART))
        }

        fun stop(ctx: Context) {
            ctx.stopService(Intent(ctx, BeaconService::class.java))
        }

        @Volatile
        var status: String = "꺼짐"
            private set
    }

    private var advertiser: BluetoothLeAdvertiser? = null
    private var advertising = false
    private var foreground = false

    private val callback = object : AdvertiseCallback() {
        override fun onStartSuccess(settingsInEffect: AdvertiseSettings) {
            advertising = true
            setStatus("신호 보내는 중")
        }

        override fun onStartFailure(errorCode: Int) {
            advertising = false
            val reason = when (errorCode) {
                ADVERTISE_FAILED_DATA_TOO_LARGE -> "데이터 크기 오류"
                ADVERTISE_FAILED_TOO_MANY_ADVERTISERS -> "다른 앱이 광고 슬롯 사용 중"
                ADVERTISE_FAILED_ALREADY_STARTED -> "이미 실행 중"
                ADVERTISE_FAILED_FEATURE_UNSUPPORTED -> "이 폰은 BLE 광고 미지원"
                else -> "내부 오류"
            }
            setStatus("시작 실패: $reason")
        }
    }

    private val btReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.getIntExtra(BluetoothAdapter.EXTRA_STATE, -1)) {
                BluetoothAdapter.STATE_ON -> startAdvertising()
                BluetoothAdapter.STATE_TURNING_OFF, BluetoothAdapter.STATE_OFF -> {
                    advertising = false
                    advertiser = null
                    setStatus("블루투스 꺼짐 (켜지면 자동 재시작)")
                }
            }
        }
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        createChannel()
        val n = buildNotification("시작 중")
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                startForeground(NOTIF_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
            } else {
                startForeground(NOTIF_ID, n)
            }
        } catch (e: Exception) {
            // 권한이 없으면 포그라운드 시작이 거부됨
            status = "권한 필요"
            stopSelf()
            return
        }
        foreground = true
        val filter = IntentFilter(BluetoothAdapter.ACTION_STATE_CHANGED)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(btReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            registerReceiver(btReceiver, filter)
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (!foreground || !BeaconConfig.isEnabled(this)) {
            stopSelf()
            return START_NOT_STICKY
        }
        if (intent?.action == ACTION_RESTART) stopAdvertising()
        startAdvertising()
        return START_STICKY
    }

    override fun onDestroy() {
        stopAdvertising()
        try {
            unregisterReceiver(btReceiver)
        } catch (_: Exception) {
        }
        status = "꺼짐"
        super.onDestroy()
    }

    @SuppressLint("MissingPermission")
    private fun startAdvertising() {
        if (advertising) return
        if (!BeaconConfig.hasAdvertisePermission(this)) {
            setStatus("블루투스 권한 필요")
            return
        }
        val adapter = (getSystemService(BLUETOOTH_SERVICE) as BluetoothManager).adapter
        if (adapter == null || !adapter.isEnabled) {
            setStatus("블루투스 꺼짐 (켜지면 자동 재시작)")
            return
        }
        val adv = adapter.bluetoothLeAdvertiser
        if (adv == null) {
            setStatus("이 폰은 BLE 광고 미지원")
            return
        }
        val settings = AdvertiseSettings.Builder()
            .setAdvertiseMode(AdvertiseSettings.ADVERTISE_MODE_LOW_POWER) // 약 1초 간격
            .setTxPowerLevel(BeaconConfig.power(this).level)
            .setConnectable(false)
            .setTimeout(0)
            .build()
        val data = AdvertiseData.Builder()
            .addManufacturerData(BeaconConfig.APPLE_COMPANY_ID, BeaconConfig.iBeaconPayload(this))
            .setIncludeDeviceName(false)
            .setIncludeTxPowerLevel(false)
            .build()
        advertiser = adv
        try {
            adv.startAdvertising(settings, data, callback)
        } catch (e: SecurityException) {
            setStatus("블루투스 권한 필요")
        }
    }

    @SuppressLint("MissingPermission")
    private fun stopAdvertising() {
        try {
            advertiser?.stopAdvertising(callback)
        } catch (_: Exception) {
        }
        advertising = false
    }

    private fun setStatus(s: String) {
        status = s
        getSystemService(NotificationManager::class.java).notify(NOTIF_ID, buildNotification(s))
    }

    private fun createChannel() {
        val ch = NotificationChannel(CHANNEL_ID, "현관 비콘", NotificationManager.IMPORTANCE_MIN).apply {
            description = "비콘이 동작 중임을 표시합니다"
            setShowBadge(false)
        }
        getSystemService(NotificationManager::class.java).createNotificationChannel(ch)
    }

    private fun buildNotification(text: String): Notification {
        val pi = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        return Notification.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_beacon)
            .setContentTitle("현관 비콘")
            .setContentText(text)
            .setContentIntent(pi)
            .setOngoing(true)
            .build()
    }
}
