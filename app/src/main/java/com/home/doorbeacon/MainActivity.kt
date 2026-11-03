package com.home.doorbeacon

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.bluetooth.BluetoothManager
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Typeface
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.provider.Settings
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast

/** 화면 하나: 켜기/끄기, ESPresense ID 확인·복사, 송신 세기, 배터리 예외 설정 */
class MainActivity : Activity() {

    private lateinit var statusText: TextView
    private lateinit var toggle: Switch
    private lateinit var powerButton: Button
    private lateinit var batteryButton: Button
    private val handler = Handler(Looper.getMainLooper())
    private val refresher = object : Runnable {
        override fun run() {
            refresh()
            handler.postDelayed(this, 1500)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val pad = dp(20)
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad, pad, pad)
        }

        root.addView(TextView(this).apply {
            text = "현관 비콘"
            textSize = 26f
            setTypeface(typeface, Typeface.BOLD)
        })

        statusText = TextView(this).apply {
            textSize = 16f
            setPadding(0, dp(8), 0, dp(16))
        }
        root.addView(statusText)

        toggle = Switch(this).apply {
            text = "비콘 켜기"
            textSize = 18f
            setOnCheckedChangeListener { _, on -> if (on) turnOn() else turnOff() }
        }
        root.addView(toggle)

        root.addView(label("ESPresense / HA 기기 ID"))
        val idText = TextView(this).apply {
            text = BeaconConfig.espresenseId(this@MainActivity)
            textSize = 13f
            setTextIsSelectable(true)
            typeface = Typeface.MONOSPACE
        }
        root.addView(idText)
        root.addView(Button(this).apply {
            text = "ID 복사"
            setOnClickListener {
                val cm = getSystemService(ClipboardManager::class.java)
                cm.setPrimaryClip(ClipData.newPlainText("beacon id", idText.text))
                Toast.makeText(this@MainActivity, "복사했어요", Toast.LENGTH_SHORT).show()
            }
        })

        root.addView(label("송신 세기"))
        powerButton = Button(this).apply {
            setOnClickListener {
                val next = (BeaconConfig.powerIndex(this@MainActivity) + 1) % BeaconConfig.POWERS.size
                BeaconConfig.setPowerIndex(this@MainActivity, next)
                if (BeaconConfig.isEnabled(this@MainActivity)) BeaconService.restart(this@MainActivity)
                refresh()
            }
        }
        root.addView(powerButton)

        root.addView(label("배터리"))
        batteryButton = Button(this).apply {
            setOnClickListener { requestBatteryException() }
        }
        root.addView(batteryButton)

        root.addView(TextView(this).apply {
            text = "갤럭시는 설정 → 애플리케이션 → 현관 비콘 → 배터리 → '제한 없음'으로 두어야 " +
                "백그라운드에서 꺼지지 않아요. 상단 알림은 비콘이 살아 있다는 표시이니 그대로 두세요."
            textSize = 13f
            setPadding(0, dp(16), 0, 0)
        })

        setContentView(ScrollView(this).apply { addView(root) })
    }

    override fun onResume() {
        super.onResume()
        handler.post(refresher)
    }

    override fun onPause() {
        handler.removeCallbacks(refresher)
        super.onPause()
    }

    private fun refresh() {
        val enabled = BeaconConfig.isEnabled(this)
        toggle.setOnCheckedChangeListener(null)
        toggle.isChecked = enabled
        toggle.setOnCheckedChangeListener { _, on -> if (on) turnOn() else turnOff() }

        statusText.text = when {
            !enabled -> "● 꺼짐"
            !isBluetoothOn() -> "● 블루투스를 켜 주세요"
            else -> "● " + BeaconService.status
        }
        powerButton.text = BeaconConfig.power(this).label

        val pm = getSystemService(PowerManager::class.java)
        val exempt = pm.isIgnoringBatteryOptimizations(packageName)
        batteryButton.text = if (exempt) "절전 예외 설정됨 ✓" else "절전 예외로 설정하기"
        batteryButton.isEnabled = !exempt
    }

    private fun turnOn() {
        val needed = mutableListOf<String>()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S &&
            checkSelfPermission(Manifest.permission.BLUETOOTH_ADVERTISE) != PackageManager.PERMISSION_GRANTED
        ) needed += Manifest.permission.BLUETOOTH_ADVERTISE
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
        ) needed += Manifest.permission.POST_NOTIFICATIONS

        if (needed.isNotEmpty()) {
            requestPermissions(needed.toTypedArray(), 1)
            return
        }
        startBeacon()
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (BeaconConfig.hasAdvertisePermission(this)) {
            startBeacon()
        } else {
            Toast.makeText(this, "'근처 기기' 권한이 있어야 비콘을 켤 수 있어요", Toast.LENGTH_LONG).show()
            refresh()
        }
    }

    private fun startBeacon() {
        BeaconConfig.setEnabled(this, true)
        BeaconService.start(this)
        refresh()
    }

    private fun turnOff() {
        BeaconConfig.setEnabled(this, false)
        BeaconService.stop(this)
        refresh()
    }

    @SuppressLint("BatteryLife")
    private fun requestBatteryException() {
        try {
            startActivity(
                Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS)
                    .setData(Uri.parse("package:$packageName"))
            )
        } catch (_: Exception) {
            startActivity(
                Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS)
                    .setData(Uri.parse("package:$packageName"))
            )
        }
    }

    private fun isBluetoothOn(): Boolean =
        getSystemService(BluetoothManager::class.java)?.adapter?.isEnabled == true

    private fun label(s: String) = TextView(this).apply {
        text = s
        textSize = 13f
        setTypeface(typeface, Typeface.BOLD)
        setPadding(0, dp(20), 0, dp(4))
        visibility = View.VISIBLE
    }

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()
}
