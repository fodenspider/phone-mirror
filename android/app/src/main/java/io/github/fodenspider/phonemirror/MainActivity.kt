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
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(32, 64, 32, 32)
        }
        status = TextView(this).apply { textSize = 17f }
        layout.addView(TextView(this).apply {
            text = "Phone Mirror · 采集验证原型\n\n此版本只验证手机硬件编码，不会向电脑传输画面。采集最长 15 秒，结果保存在应用私有目录，不会自动上传。\n"
            textSize = 20f
        })
        layout.addView(Button(this).apply {
            text = "授权并开始 15 秒采集"
            setOnClickListener {
                if (CaptureService.active) return@setOnClickListener
                val manager = getSystemService(MediaProjectionManager::class.java)
                @Suppress("DEPRECATION")
                startActivityForResult(manager.createScreenCaptureIntent(), 100)
            }
        })
        layout.addView(Button(this).apply {
            text = "停止采集"
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
        status.text = if (CaptureService.active) "正在采集，最长 15 秒；可随时从通知停止。"
            else getSharedPreferences("capture", MODE_PRIVATE).getString("status", "尚未采集。")
    }
    @Deprecated("Activity result bridge for dependency-free P0")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == 100 && resultCode == RESULT_OK && data != null) {
            startForegroundService(Intent(this, CaptureService::class.java).apply {
                putExtra("resultCode", resultCode); putExtra("projectionData", data)
            })
            status.text = "正在启动采集；完成后点击刷新状态。"
        } else if (requestCode == 100) status.text = "未授权，未开始采集。"
    }
}
