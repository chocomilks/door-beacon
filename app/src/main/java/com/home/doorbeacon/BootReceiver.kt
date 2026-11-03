package com.home.doorbeacon

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

/** 폰을 켜거나 앱이 업데이트되면 비콘을 자동으로 다시 켭니다. */
class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val action = intent.action
        if (action != Intent.ACTION_BOOT_COMPLETED && action != Intent.ACTION_MY_PACKAGE_REPLACED) return
        if (BeaconConfig.isEnabled(context) && BeaconConfig.hasAdvertisePermission(context)) {
            try {
                BeaconService.start(context)
            } catch (_: Exception) {
            }
        }
    }
}
