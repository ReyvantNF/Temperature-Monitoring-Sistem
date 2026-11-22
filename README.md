Kelompok Paus

Anggota kelompok:
Reyvant Naufal Faza (ketua) - 101012500014
Raya Permana Elfaris
Celcia Artika Saputri
Shakilla laiqa Andrisma

# Sistem Monitoring Suhu IoT (ESP32 + MQTT + Telegram)

Sistem monitoring suhu dan kelembapan berbasis **ESP32** dan sensor **DHT22**. Data dikirim lewat **MQTT** ke broker **EMQX Cloud**, ditampilkan secara real-time di **website dashboard**, dan setiap perubahan kondisi penting dikirim sebagai **notifikasi Telegram**. Proyek ini dijalankan di simulator **Wokwi** lewat VS Code dan PlatformIO.

## Fitur

- Pembacaan suhu dan kelembapan setiap 2 detik (DHT22).
- Tiga status suhu: **Dingin**, **Normal**, **Panas**, ditandai dengan LED biru, hijau, dan merah.
- Buzzer berbunyi saat suhu panas.
- Relay pendingin (AC) menyala otomatis saat suhu tinggi, dan bisa dinyalakan manual lewat MQTT.
- Tampilan lokal di layar OLED SSD1306.
- Pengiriman data ke EMQX lewat MQTT TLS (port 8883).
- Website dashboard: suhu, kelembapan, status relay, nilai min/maks/rata-rata, grafik 60 data terakhir, dan riwayat perubahan status.
- Notifikasi Telegram saat status berubah, dengan jeda minimal antar pesan dan pengingat berkala selama suhu panas.
- Mode demo di website untuk mencoba tampilan tanpa perangkat.

## Arsitektur

```
ESP32 (DHT22) ──MQTT TLS 8883──► EMQX Cloud ──WebSocket TLS 8084──► Website dashboard
      │
      └──HTTPS──► Telegram Bot API ──► Notifikasi di HP
```

Notifikasi Telegram dikirim langsung dari ESP32, bukan dari website, sehingga tetap berjalan walaupun halaman web tidak dibuka. Token bot juga tidak pernah berada di sisi browser.

## Komponen

| Komponen | Fungsi |
|---|---|
| ESP32 DevKit | Mikrokontroler utama dengan WiFi |
| DHT22 | Sensor suhu dan kelembapan |
| OLED SSD1306 128x64 (I2C) | Tampilan lokal |
| LED merah, biru, hijau | Indikator status suhu |
| Buzzer | Alarm saat suhu panas |
| Modul relay | Mengendalikan pendingin (AC) |

### Pemetaan pin

| Perangkat | Pin ESP32 |
|---|---|
| DHT22 (data) | GPIO 4 |
| LED merah | GPIO 12 |
| LED biru | GPIO 14 |
| LED hijau | GPIO 27 |
| Buzzer | GPIO 25 |
| Relay | GPIO 26 |
| OLED (I2C) | SDA GPIO 21, SCL GPIO 22 (default ESP32) |

## Logika sistem

| Suhu | Status | LED | Buzzer |
|---|---|---|---|
| di bawah 20 °C | Dingin | Biru | Mati |
| 20 sampai 30 °C | Normal | Hijau | Mati |
| di atas 30 °C | Panas | Merah | Berbunyi |

- **Relay (AC)** menyala jika suhu di atas 28 °C **atau** mode manual aktif.
- **Telegram** dikirim saat status berubah, dengan jeda minimal 30 detik antar pesan. Selama status Panas, pengingat dikirim ulang setiap 5 menit.
- Batas suhu dapat diubah di bagian atas `src/main.cpp` (`TEMP_DINGIN`, `TEMP_PANAS`, `TEMP_AC_ON`). Nilai yang sama perlu disesuaikan di `index.html`.

## Format data MQTT

| Topic | Arah | Isi |
|---|---|---|
| `tempmon/data` | ESP32 ke broker (retained) | Data sensor dalam JSON |
| `tempmon/control` | Broker ke ESP32 | `1` untuk AC manual ON, `0` untuk OFF |

Contoh payload `tempmon/data`:

```json
{"temp": 28.5, "hum": 60.0, "status": "NORMAL", "relay": false}
```

## Struktur proyek

```
.
├── src/
│   └── main.cpp        # Program ESP32
├── index.html          # Website dashboard (satu file)
├── diagram.json        # Rangkaian Wokwi
├── wokwi.toml          # Konfigurasi simulator Wokwi
├── platformio.ini      # Konfigurasi PlatformIO dan library
└── README.md
```

## Persiapan

### 1. Broker EMQX

1. Buat deployment **EMQX Serverless** di EMQX Cloud.
2. Di *Access Control, Authentication*, buat **dua user MQTT**:
   - satu untuk ESP32 (boleh publish dan subscribe),
   - satu untuk website (cukup subscribe ke `tempmon/#`).
3. Catat alamat broker dari halaman *Deployment Overview*. Port yang dipakai: **8883** (MQTT TLS) untuk ESP32 dan **8084** (WebSocket TLS) untuk website.

Gunakan user yang berbeda untuk website karena kredensialnya dapat dilihat siapa saja lewat browser.

### 2. Bot Telegram

1. Buka `@BotFather`, kirim `/newbot`, lalu ikuti instruksinya dan simpan **token**.
2. Buka bot yang baru dibuat dan tekan **Start**.
3. Ambil **Chat ID** Anda lewat `@userinfobot`.

### 3. Isi konfigurasi

Repository ini sengaja memakai placeholder `ISI_SENDIRI` agar tidak ada rahasia yang terunggah. Isi di komputer Anda sendiri:

**`src/main.cpp`**

```cpp
const char* MQTT_HOST = "ALAMAT-BROKER-ANDA";
const char* MQTT_USER = "ISI_SENDIRI";   // user MQTT untuk ESP32
const char* MQTT_PASS = "ISI_SENDIRI";
const char* TG_TOKEN   = "ISI_SENDIRI";  // token dari BotFather
const char* TG_CHAT_ID = "ISI_SENDIRI";  // angka dari @userinfobot
```

**`index.html`** (blok `CONFIG`)

```js
const CONFIG = {
  host: "ALAMAT-BROKER-ANDA",
  port: 8084,
  path: "/mqtt",
  username: "ISI_SENDIRI",   // user khusus website
  password: "ISI_SENDIRI",
  topic: "tempmon/data"
};
```

> **Peringatan:** jangan commit file yang sudah berisi password atau token. Jika terlanjur terunggah, segera ganti token lewat `/revoke` di BotFather dan ganti password user di EMQX.

## Cara menjalankan

### Simulasi di VS Code

1. Install ekstensi **PlatformIO IDE** dan **Wokwi Simulator**.
2. Buka folder proyek, lalu **Build** (ikon centang di bar bawah, atau `Ctrl + Alt + B`) sampai muncul `SUCCESS`.
3. Pastikan `wokwi.toml` mengarah ke hasil build:

   ```toml
   [wokwi]
   version = 1
   firmware = '.pio/build/esp32dev/firmware.bin'
   elf = '.pio/build/esp32dev/firmware.elf'
   ```

4. Buka `diagram.json`, lalu klik tombol **Play**. Saat pertama kali, ikuti proses lisensi gratis Wokwi (Community License).
5. Di Serial Monitor simulasi harus muncul `MQTT terhubung` dan `Telegram HTTP 200`.

Setiap kali kode diubah, lakukan **Build ulang** sebelum menjalankan simulasi.

### Website dashboard

Buka `index.html` di browser. Pojok kanan atas harus menampilkan **Terhubung ke EMQX**. Tombol **Coba mode demo** menampilkan data acak tanpa perangkat.

Untuk hosting gratis, file ini dapat diunggah ke GitHub Pages atau Netlify (gunakan user MQTT khusus subscribe).

### Cara menguji

1. Jalankan simulasi, lalu buka `index.html`.
2. Klik sensor DHT22 di Wokwi dan geser suhu ke 35 °C.
3. Hasil yang diharapkan: LED merah dan buzzer aktif, relay menyala, website berubah merah, dan Telegram menerima peringatan.
4. Turunkan suhu ke 25 °C untuk melihat status kembali Normal.

## Pemecahan masalah

| Gejala | Kemungkinan penyebab |
|---|---|
| `MQTT gagal, rc=5` | Username atau password MQTT salah, atau tidak punya izin |
| `MQTT gagal, rc=-2` | Broker tidak terjangkau (cek WiFi dan alamat broker) |
| `Telegram HTTP 401` | Token bot salah |
| `Telegram HTTP 400` | Chat ID salah, atau bot belum di-Start |
| `Telegram HTTP -1` | Koneksi ke Telegram gagal, jalankan ulang simulasi |
| Website tidak menampilkan data | Topic berbeda, atau user website tidak boleh subscribe |
| Simulasi memakai kode lama | Lupa Build ulang setelah mengubah kode |

## Teknologi

- ESP32 (Arduino framework) dengan PlatformIO
- Library: `DHT sensor library`, `Adafruit Unified Sensor`, `PubSubClient`, `Adafruit GFX`, `Adafruit SSD1306`
- EMQX Cloud (MQTT broker)
- MQTT.js untuk koneksi WebSocket di website
- Telegram Bot API
- Wokwi (simulator)

## Pengembangan lanjutan

- Kontrol relay dari website.
- Penyimpanan riwayat data jangka panjang.
- Penambahan sensor kedua untuk memantau dua area.
- Memindahkan rahasia ke `secrets.h` yang dimasukkan ke `.gitignore`.


