package com.home.doorbeacon

import android.Manifest
import android.bluetooth.le.AdvertiseSettings
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import java.nio.ByteBuffer
import java.util.UUID

/** 비콘 설정값. 설치할 때 한 번 만든 UUID를 계속 씁니다. */
object BeaconConfig {
    const val MAJOR = 100
    const val MINOR = 1
    const val APPLE_COMPANY_ID = 0x004C

    private const val PREFS = "beacon"
    private const val KEY_UUID = "uuid"
    private const val KEY_ENABLED = "enabled"
    private const val KEY_POWER = "power"

    /** 송신 세기: 이름, Android 출력 레벨, 1m 기준 RSSI(대략값, ESPresense에서 보정) */
    data class Power(val label: String, val level: Int, val rssiAt1m: Int)

    val POWERS = listOf(
        Power("약하게 (가장 절전)", AdvertiseSettings.ADVERTISE_TX_POWER_LOW, -75),
        Power("보통 (권장)", AdvertiseSettings.ADVERTISE_TX_POWER_MEDIUM, -66),
        Power("강하게 (인식 우선)", AdvertiseSettings.ADVERTISE_TX_POWER_HIGH, -59),
    )

    private fun prefs(ctx: Context) = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    fun uuid(ctx: Context): String {
        val p = prefs(ctx)
        return p.getString(KEY_UUID, null) ?: UUID.randomUUID().toString().also {
            p.edit().putString(KEY_UUID, it).apply()
        }
    }

    /** ESPresense / HA에서 쓰는 기기 ID */
    fun espresenseId(ctx: Context) = "iBeacon:${uuid(ctx)}-$MAJOR-$MINOR"

    fun isEnabled(ctx: Context) = prefs(ctx).getBoolean(KEY_ENABLED, false)
    fun setEnabled(ctx: Context, on: Boolean) = prefs(ctx).edit().putBoolean(KEY_ENABLED, on).apply()

    fun powerIndex(ctx: Context) = prefs(ctx).getInt(KEY_POWER, 1).coerceIn(0, POWERS.lastIndex)
    fun setPowerIndex(ctx: Context, i: Int) = prefs(ctx).edit().putInt(KEY_POWER, i).apply()
    fun power(ctx: Context) = POWERS[powerIndex(ctx)]

    fun hasAdvertisePermission(ctx: Context): Boolean =
        Build.VERSION.SDK_INT < Build.VERSION_CODES.S ||
            ctx.checkSelfPermission(Manifest.permission.BLUETOOTH_ADVERTISE) == PackageManager.PERMISSION_GRANTED

    /** iBeacon 광고 데이터 (Apple 제조사 데이터 23바이트) */
    fun iBeaconPayload(ctx: Context): ByteArray {
        val u = UUID.fromString(uuid(ctx))
        return ByteBuffer.allocate(23).apply {
            put(0x02.toByte())
            put(0x15.toByte())
            putLong(u.mostSignificantBits)
            putLong(u.leastSignificantBits)
            putShort(MAJOR.toShort())
            putShort(MINOR.toShort())
            put(power(ctx).rssiAt1m.toByte())
        }.array()
    }
}
