package io.github.fodenspider.phonemirror

import android.app.Activity
import android.content.Intent
import android.media.projection.MediaProjectionManager
import android.os.Bundle
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView

class MainActivity : Activity() {
    private lateinit var status: TextView
    private var live = false
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        live = savedInstanceState?.getBoolean("live") ?: false
        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(32, 64, 32, 32)
        }
        status = TextView(this).apply { textSize = 17f }
        layout.addView(TextView(this).apply {
            text = "Phone Mirror · USB 实时预览\n\nUSB 模式向已授权电脑实时共享画面，不保存录像。每次连接都需要屏幕共享授权；旋转屏幕后需要重新连接。\n本地采集保留 15 秒文件，供离线验证。\n"
            textSize = 20f
        })
        layout.addView(Button(this).apply {
            text = "授权并开始 USB 实时投屏"
            setOnClickListener { authorize(true) }
        })
        layout.addView(Button(this).apply {
            text = "授权并开始 15 秒采集"
            setOnClickListener {
                authorize(false)
            }
        })
        layout.addView(Button(this).apply {
            text = "停止共享 / 采集"
            setOnClickListener { stopService(Intent(this@MainActivity, CaptureService::class.java)) }
        })
        layout.addView(Button(this).apply {
            text = "刷新采集状态"
            setOnClickListener { refresh() }
        })
        layout.addView(status)
        setContentView(layout)
    }
    override fun onResume() { super.onResume(); refresh() }
    private fun refresh() {
        status.text = if (CaptureService.active) CaptureService.liveStatus
            else getSharedPreferences("capture", MODE_PRIVATE).getString("status", "尚未采集。")
    }
    private fun authorize(usb: Boolean) {
        if (CaptureService.active) { refresh(); return }
        live = usb
        val manager = getSystemService(MediaProjectionManager::class.java)
        @Suppress("DEPRECATION")
        startActivityForResult(manager.createScreenCaptureIntent(), 100)
    }
    override fun onSaveInstanceState(outState: Bundle) {
        outState.putBoolean("live", live); super.onSaveInstanceState(outState)
    }
    @Deprecated("Activity result bridge for dependency-free P0")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == 100 && resultCode == RESULT_OK && data != null) {
            startForegroundService(Intent(this, CaptureService::class.java).apply {
                putExtra("resultCode", resultCode); putExtra("projectionData", data)
                putExtra("live", live)
            })
            status.text = if (live) "等待电脑通过 USB 连接，请在电脑运行启动脚本。" else "正在启动采集；完成后点击刷新状态。"
        } else if (requestCode == 100) status.text = "未授权，未开始采集。"
    }
}
