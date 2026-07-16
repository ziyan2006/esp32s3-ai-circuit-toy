#ifndef CONFIG_H
#define CONFIG_H

/* ================= Wi-Fi Configuration ================= */
#define WIFI_SSID           "YOUR_WIFI_SSID"
#define WIFI_PASSWORD       "YOUR_WIFI_PASSWORD"
#define WIFI_RETRY_MAX      5

/* ================= Volcengine API Configuration ================= */
#define VOLCENGINE_WS_URL   "wss://openspeech.bytedance.com/api/v3/realtime/dialogue"
#define VOLCENGINE_APP_ID   "YOUR_APP_ID"
#define VOLCENGINE_API_KEY  "YOUR_API_KEY"
#define VOLCENGINE_APP_KEY  "YOUR_APP_KEY"

#define VOLCENGINE_MODEL_NAME "O"
#define VOLCENGINE_VOICE_TYPE "zh_female_vv_jupiter_bigtts"

/* ================= Audio Configuration ================= */
#define AUDIO_SAMPLE_RATE     16000
#define AUDIO_BITS_PER_SAMPLE 16
#define AUDIO_CHANNELS        1

#define CODEC_I2C_SDA_PIN 41
#define CODEC_I2C_SCL_PIN 40
#define CODEC_I2C_PORT    I2C_NUM_0

#define I2S_PORT_NUM   I2S_NUM_0
#define I2S_MCLK_PIN   2
#define I2S_BCLK_PIN   17
#define I2S_LRCK_PIN   45
#define I2S_DOUT_PIN   15
#define I2S_DIN_PIN    16
#define SPEAKER_PA_PIN 46

#endif // CONFIG_H
