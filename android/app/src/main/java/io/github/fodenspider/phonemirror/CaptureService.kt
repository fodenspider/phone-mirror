package io.github.fodenspider.phonemirror

import android.app.*
import android.content.Intent
import android.content.pm.ServiceInfo
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.media.*
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.*
import android.view.Surface
import android.view.WindowManager
import java.io.BufferedOutputStream
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** User-consented local probe or single-use, loopback-only USB video session. */
class CaptureService : Service() {
    companion object {
        @Volatile var active = false
        @Volatile var liveStatus = ""
        @Volatile var controlGrant: Any? = null
        @Volatile var captureAspect = 0.0
        private const val CHANNEL = "capture"
    }
    private val worker = HandlerThread("capture-probe")
    private lateinit var handler: Handler
    private var projection: MediaProjection? = null
    private var codec: MediaCodec? = null
    private var surface: Surface? = null
    private var display: VirtualDisplay? = null
    private var output: BufferedOutputStream? = null
    private var usb: UsbStreamer? = null
    private var live = false
    private var allowControl = false
    private var stopped = false
    private var frames = 0L
    private var sequence = 0L
    private var bytesWritten = 0L
    private var width = 0
    private var height = 0
    private var encoderName = ""
    private val callback = object : MediaProjection.Callback() {
        override fun onStop() { stopCapture("系统已停止投屏") }
        override fun onCapturedContentResize(w: Int, h: Int) {
            // P0 preserves aspect ratio by ending, rather than silently stretching.
            if (width > 0 && height > 0 && kotlin.math.abs(w.toDouble() / h - width.toDouble() / height) > 0.05)
                stopCapture("捕获区域比例变化，请重新采集")
        }
    }
    override fun onCreate() {
        super.onCreate(); worker.start(); handler = Handler(worker.looper)
    }
    override fun onBind(intent: Intent?) = null
    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == "STOP") { stopSelf(); return START_NOT_STICKY }
        if (active || stopped) return START_NOT_STICKY
        @Suppress("DEPRECATION")
        val data = intent?.getParcelableExtra<Intent>("projectionData")
        if (data == null) { stopSelf(); return START_NOT_STICKY }
        getSystemService(NotificationManager::class.java).createNotificationChannel(
            NotificationChannel(CHANNEL, "屏幕采集", NotificationManager.IMPORTANCE_LOW))
        val stop = PendingIntent.getService(this, 1,
            Intent(this, CaptureService::class.java).setAction("STOP"),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
        live = intent?.getBooleanExtra("live", false) == true
        allowControl = live && intent?.getBooleanExtra("control", false) == true
        liveStatus = if (live) "等待电脑通过 USB 连接；两分钟未连接会自动结束" else "正在进行 15 秒本地采集"
        val notification = Notification.Builder(this, CHANNEL)
            .setSmallIcon(android.R.drawable.ic_menu_camera)
            .setContentTitle(if (live) "Phone Mirror · USB 屏幕共享" else "Phone Mirror · 本地采集")
            .setContentText(if (live) "仅向本次授权的 USB 电脑共享，点击停止结束" else "最长 15 秒，仅保存在本机")
            .setOngoing(true).addAction(Notification.Action.Builder(null, "停止", stop).build()).build()
        startForeground(1, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION)
        active = true
        val code = intent?.getIntExtra("resultCode", Activity.RESULT_CANCELED) ?: Activity.RESULT_CANCELED
        handler.post {
            try {
                if (live) {
                    usb = UsbStreamer(File(filesDir, "usb-session.json"), {
                        handler.post {
                            if (!stopped) try {
                                liveStatus = "USB 已连接，正在共享画面"
                                startCapture(code, data)
                            } catch (e: Exception) { stopCapture("采集失败：${e.javaClass.simpleName}") }
                        }
                    }, { reason -> handler.post { stopCapture(reason) } }, { gesture ->
                        GestureControlService.instance?.submit(gesture)
                    })
                    usb!!.start()
                } else startCapture(code, data)
            }
            catch (e: Exception) { stopCapture("采集失败：${e.javaClass.simpleName}: ${e.message}") }
        }
        return START_NOT_STICKY
    }
    private fun startCapture(resultCode: Int, data: Intent) {
        val manager = getSystemService(MediaProjectionManager::class.java)
        projection = manager.getMediaProjection(resultCode, data)
        projection!!.registerCallback(callback, handler)
        val bounds = getSystemService(WindowManager::class.java).maximumWindowMetrics.bounds
        val scale = minOf(1.0, 1280.0 / maxOf(bounds.width(), bounds.height()))
        width = (bounds.width() * scale).toInt() / 2 * 2
        height = (bounds.height() * scale).toInt() / 2 * 2
        captureAspect = width.toDouble() / height
        val format = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, width, height).apply {
            setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
            setInteger(MediaFormat.KEY_BIT_RATE, 4_000_000)
            setInteger(MediaFormat.KEY_FRAME_RATE, 30)
            setFloat(MediaFormat.KEY_MAX_FPS_TO_ENCODER, 30f)
            setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1)
            setInteger(MediaFormat.KEY_MAX_B_FRAMES, 0)
            if (live) setLong(MediaFormat.KEY_REPEAT_PREVIOUS_FRAME_AFTER, 1_000_000L)
        }
        val candidates = MediaCodecList(MediaCodecList.REGULAR_CODECS).codecInfos.filter {
            it.isEncoder && it.isHardwareAccelerated && it.supportedTypes.contains(MediaFormat.MIMETYPE_VIDEO_AVC)
        }
        val selected = candidates.firstOrNull {
            runCatching { it.getCapabilitiesForType(MediaFormat.MIMETYPE_VIDEO_AVC).isFormatSupported(format) }.getOrDefault(false)
        } ?: error("No hardware H.264 encoder supports the probe format")
        encoderName = selected.name
        codec = MediaCodec.createByCodecName(encoderName)
        codec!!.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
        surface = codec!!.createInputSurface()
        // One bounded file replaces the previous probe; no screen data is logged.
        if (!live) output = BufferedOutputStream(File(filesDir, "capture.pmstream").outputStream())
        codec!!.start()
        display = projection!!.createVirtualDisplay("PhoneMirrorProbe", width, height,
            resources.displayMetrics.densityDpi, DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
            surface, null, handler)
        if (allowControl) controlGrant = Any()
        if (!live) handler.postDelayed({ stopCapture("15 秒采集完成") }, 15_000)
        else handler.postDelayed({ stopCapture("预览会话已达一小时，请重新授权") }, 3_600_000)
        handler.post(::drain)
    }
    private fun drain() {
        if (stopped) return
        try {
            val c = codec ?: return
            val info = MediaCodec.BufferInfo()
            // Bound callback work so stop/permission callbacks cannot starve.
            repeat(8) {
                val index = c.dequeueOutputBuffer(info, 0)
                if (index == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                    val f = c.outputFormat
                    val sps = copy(f.getByteBuffer("csd-0"))
                    val pps = copy(f.getByteBuffer("csd-1"))
                    val config = ByteBuffer.allocate(20 + sps.size + pps.size).order(ByteOrder.BIG_ENDIAN)
                        .putInt(width).putInt(height).putInt(1).putInt(sps.size).putInt(pps.size)
                        .put(sps).put(pps).array()
                    write(Protocol.VIDEO_CONFIG, config, 0, 0)
                } else if (index >= 0) {
                    try {
                        if (info.size > 0 && info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG == 0) {
                            require(info.size <= Protocol.limit(Protocol.VIDEO_ACCESS_UNIT))
                            val buffer = c.getOutputBuffer(index) ?: error("Missing output buffer")
                            buffer.position(info.offset); buffer.limit(info.offset + info.size)
                            val data = ByteArray(info.size); buffer.get(data)
                            write(Protocol.VIDEO_ACCESS_UNIT, data,
                                if (info.flags and MediaCodec.BUFFER_FLAG_KEY_FRAME != 0) 1 else 0,
                                info.presentationTimeUs)
                            frames++
                        }
                    } finally { c.releaseOutputBuffer(index, false) }
                }
            }
            handler.postDelayed(::drain, 5)
        } catch (e: Exception) { stopCapture("采集失败：${e.javaClass.simpleName}: ${e.message}") }
    }
    private fun copy(buffer: ByteBuffer?): ByteArray {
        if (buffer == null) return byteArrayOf()
        val copy = buffer.duplicate()
        require(copy.remaining() <= 32 * 1024)
        return ByteArray(copy.remaining()).also { copy.get(it) }
    }
    private fun write(type: Int, payload: ByteArray, flags: Int, timestamp: Long) {
        if (!live) check(bytesWritten + Protocol.HEADER_SIZE + payload.size <= 32L * 1024 * 1024) { "Capture file budget exceeded" }
        val header = Protocol.encode(Protocol.Header(type, flags, payload.size, 1, 1, sequence++, timestamp))
        if (live) usb!!.offer(header, payload) else { output!!.write(header); output!!.write(payload) }
        bytesWritten += header.size + payload.size
    }
    private fun stopCapture(reason: String) {
        if (stopped) return
        controlGrant = null
        stopped = true; handler.removeCallbacksAndMessages(null)
        usb?.finish(Protocol.encode(Protocol.Header(4, streamId = 1, streamEpoch = 1, sequence = sequence)))
        usb = null
        display?.release(); display = null
        runCatching { codec?.stop() }; runCatching { codec?.release() }; codec = null
        surface?.release(); surface = null
        projection?.unregisterCallback(callback); projection?.stop(); projection = null
        val closeError = runCatching { output?.close() }.exceptionOrNull(); output = null
        getSharedPreferences("capture", MODE_PRIVATE).edit().putString("status",
            "$reason\n编码器：$encoderName\n尺寸：${width}×$height\n帧数：$frames\n" +
                (if (live) "USB 会话已结束，未保存实时画面" else "文件：capture.pmstream（$bytesWritten 字节）") +
                if (closeError != null) "\n文件关闭失败，文件可能不完整" else "").apply()
        liveStatus = reason; stopSelf()
    }
    override fun onDestroy() {
        handler.post { stopCapture("用户停止共享"); active = false; worker.quitSafely() }
        super.onDestroy()
    }
}
