/**
 * ESP32-CAM_Interval.ino - Capture pictures to SD card at set interval.
 *
 * Based on the code from https://robotzero.one/time-lapse-esp32-cameras/
 * Original Copyright: Copyright (c) 2019, Robot Zero One
 *
 * Copyright (c) 2019, David Imhoff <dimhoff.devel@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the author nor the names of its contributors may
 *       be used to endorse or promote products derived from this software
 *       without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "config.h"

#include "Arduino.h"

#include "esp_camera.h"

#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

// GPIO (rtc_gpio_hold_en())
#include "driver/rtc_io.h"

// MicroSD
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include <dirent.h>

#include "io_defs.h"
#include "camera.h"
#include "configuration.h"
#include "exif.h"
#include "setup_mode.h"

#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))

// Time unit defines
#define MSEC_AS_USEC (1000L)
#define SEC_AS_USEC (1000L * MSEC_AS_USEC)

// Minimum sleep time.
// If next capture is less then this many micro seconds away, then stay awake.
// FIXME: This can probably be reduced if WITH_EVIL_CAM_PWR_SHUTDOWN is
//        disabled, test this.
#define MIN_SLEEP_TIME (15 * SEC_AS_USEC)
// Wake-up this many micro seconds before capture time to allow initialization.
#define WAKE_USEC_EARLY (6 * SEC_AS_USEC)

// Timelapse directory name format: /sdcard/timelapseXXXX/
#define CAPTURE_DIR_PREFIX "timelapse"
#define CAPTURE_DIR_PREFIX_LEN 9

// RTC memory storage
RTC_DATA_ATTR struct {
	struct timeval next_capture_time;
} nv_data;

// Globals
static bool setup_mode = false;
static char capture_path[8 + CAPTURE_DIR_PREFIX_LEN + 4 + 1];
static struct timeval capture_interval_tv;
static struct timeval next_capture_time;

/************************ Initialization ************************/
void setup()
{
  bool is_wakeup = false;

  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED) {
    is_wakeup = true;
  }

  Serial.begin(115200);
  Serial.println();
  print_capability();

  // Configure red LED
  pinMode(LED_GPIO_NUM, OUTPUT);
  digitalWrite(LED_GPIO_NUM, HIGH);

  // Check if set-up button is pressed
  if (esp_reset_reason() == ESP_RST_POWERON) {
#ifdef WITH_SETUP_MODE_BUTTON
    pinMode(BTN_GPIO_NUM, INPUT_PULLUP);
    delay(10);
    // TODO: Do some debouncing/filtering
    if (digitalRead(BTN_GPIO_NUM) == LOW) {
      setup_mode = true;
    }
#else // WITH_SETUP_MODE_BUTTON
    setup_mode = true;
#endif // WITH_SETUP_MODE_BUTTON
  }

  // Init SD Card
  if (!init_sdcard()) {
    goto fail;
  }

#ifdef WITH_FLASH
  // WORKAROUND:
  // Force Flash LED off on AI Thinker boards.
  // This is needed because resistors R11, R12 and R13 form a voltage divider
  // that causes a voltage of about 0.57 Volt on the base of transistor Q1.
  // This will dimly light up the flash LED.
  rtc_gpio_hold_dis(gpio_num_t(FLASH_GPIO_NUM));
  pinMode(FLASH_GPIO_NUM, OUTPUT);
  digitalWrite(FLASH_GPIO_NUM, LOW);
#endif // WITH_FLASH
  
  // TODO: error log to file?

  // Load config file
  if (!cfg.loadConfig()) {
    if (setup_mode) {
      Serial.println("Ignoring bad configuration file because in set-up mode");
    } else {
      goto fail;
    }
  }
  update_exif_from_cfg(cfg);
  capture_interval_tv.tv_sec = cfg.getCaptureInterval() / 1000;
  capture_interval_tv.tv_usec = (cfg.getCaptureInterval() % 1000) * 1000;

  // Set timezone
  setenv("TZ", cfg.getTzInfo(), 1);
  tzset();

  // Get current time and if time is not set, run setup
  {
    time_t now = time(NULL);
    struct tm tm_now;
    Serial.printf("Current time: %s", ctime(&now));
    Serial.println();
    localtime_r(&now, &tm_now);
    if (tm_now.tm_year + 1900 < 2021) {
      setup_mode = true;
    }
  }

  // Inititialize next capture time
  if (is_wakeup) {
    next_capture_time = nv_data.next_capture_time;
    Serial.printf("Next image at: %s", ctime(&next_capture_time.tv_sec));
    Serial.println();
  } else {
    (void) gettimeofday(&next_capture_time, NULL);
  }

  if (setup_mode) {
    // Set-up Mode
    if (!setup_mode_init()) {
      goto fail;
    }
  } else {
    // Initialize capture directory
    if (!init_capture_dir(is_wakeup)) {
      goto fail;
    }
  }

  // camera init
  if (!camera_init()) {
    goto fail;
  }

  if (setup_mode) {
    Serial.println("--- Initialization Done, Entering Setup Mode ---");
  } else {
    Serial.println("--- Initialization Done ---");
  }

  return;

fail:
  // TODO: write dead program for ULP to blink led while in deep sleep, instead of waste power with main CPU
  while (true) {
    const static long blink_sequence[] = { 500, 500 };
    static long blink_last = 0;
    static uint8_t blink_idx = 0;

    // Blink LED
    long now = millis();
    if (now - blink_last > blink_sequence[blink_idx]) {
      blink_idx = (blink_idx + 1) % ARRAY_SIZE(blink_sequence);
      blink_last = now;
      digitalWrite(LED_GPIO_NUM, (blink_idx & 1) ? HIGH : LOW);
    }
  }
}

/**
 * Mount SD Card
 */
static bool init_sdcard()
{
  esp_err_t ret = ESP_FAIL;
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
    .format_if_mount_failed = false,
    .max_files = 1,
  };
  sdmmc_card_t *card;

#ifdef WITH_SD_4BIT
  host.flags = SDMMC_HOST_FLAG_4BIT;
  slot_config.width = 4;
#else
  host.flags = SDMMC_HOST_FLAG_1BIT;
  slot_config.width = 1;
#endif

  Serial.print("Mounting SD card... ");
  ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config,
                                &card);
  if (ret == ESP_OK) {
    Serial.println("Done");
  }  else  {
    Serial.println("FAILED");
    Serial.printf("Failed to mount SD card VFAT filesystem. Error: %s\n",
                     esp_err_to_name(ret));
    return false;
  }

  return true;
}

/**
 * Create new directory to store images
 */
static bool init_capture_dir(bool reuse_last_dir)
{
  DIR *dirp;
  struct dirent *dp;
  int dir_idx = 0;

  // Find unused directory name
  if ((dirp = opendir("/sdcard/")) == NULL) {
    Serial.println("couldn't open directory /sdcard/");
    return -1;
  }

  do {
    errno = 0;
    if ((dp = readdir(dirp)) != NULL) {
      if (strlen(dp->d_name) != CAPTURE_DIR_PREFIX_LEN + 4)
        continue;
      if (strncmp(dp->d_name, CAPTURE_DIR_PREFIX,
          CAPTURE_DIR_PREFIX_LEN) != 0)
        continue;

      char *endp;
      int idx = strtoul(&(dp->d_name[CAPTURE_DIR_PREFIX_LEN]), &endp, 10);
      if (*endp != '\0')
        continue;

      if (idx > dir_idx) {
        dir_idx = idx;
      }
    }
  } while (dp != NULL);

  (void) closedir(dirp);

  if (errno != 0) {
    Serial.println("Error reading directory /sdcard/");
    return false;
  }

  if (!reuse_last_dir) {
    dir_idx += 1;
  }

  // Create new dir
  snprintf(capture_path, sizeof(capture_path),
	"/sdcard/" CAPTURE_DIR_PREFIX "%04u", dir_idx);
  
  if (!reuse_last_dir) {
    if (mkdir(capture_path, 0644) != 0) {
      Serial.print("Failed to create directory: ");
      Serial.println(capture_path);
      return false;
    }
  }

  Serial.print("Storing pictures in: ");
  Serial.println(capture_path);

  return true;
}

/************************ Main ************************/

/**
 * Take picture and save to SD card
 */
static void save_photo()
{
  camera_fb_t *fb;

  if (cfg.getEnableBusyLed()) {
    digitalWrite(LED_GPIO_NUM, LOW);
  }

  // Capture image
  fb = camera_capture();

  // Generate filename
  time_t now = time(NULL);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  char filename[sizeof(capture_path) + 15 + 4 + 1];
  size_t capture_path_len = strlen(capture_path);
  strcpy(filename, capture_path);
  strftime(&filename[capture_path_len], sizeof(filename) - capture_path_len,
             "/%Y%m%d_%H%M%S.jpg", &timeinfo);

  // Generate Exif header
  const uint8_t *exif_header = NULL;
  size_t exif_len = 0;
  get_exif_header(fb, &exif_header, &exif_len);

  size_t data_offset = get_jpeg_data_offset(fb);

  // Save picture
  FILE *file = fopen(filename, "w");
  if (file != NULL)  {
    size_t ret = 0;
    if (exif_header != NULL) {
      ret = fwrite(exif_header, exif_len, 1, file);
      if (ret != 1) {
        Serial.println("Failed\nError while writing header to file");
        data_offset = 0;
      }
    } else {
        data_offset = 0;
    }

    ret = fwrite(&fb->buf[data_offset], fb->len - data_offset, 1, file);
    if (ret != 1) {
      Serial.println("Failed\nError while writing to file");
    } else {
      Serial.printf("Saved as %s", filename);
      Serial.println();
    }
    fclose(file);
  } else {
    Serial.printf("Failed\nCould not open file: %s", filename);
    Serial.println();
  }

  camera_fb_return(fb);
  
  if (cfg.getEnableBusyLed()) {
    digitalWrite(LED_GPIO_NUM, HIGH);
  }
}

void loop()
{
  if (setup_mode) {
    setup_mode_loop();
    return;
  }

  // Take picture if interval passed
  // NOTE: This breaks if clock jumps are introduced. Make sure to use
  // adjtime().
  struct timeval now;
  (void) gettimeofday(&now, NULL);
  if (!timercmp(&now, &next_capture_time, <)) {
    save_photo();

    timeradd(&next_capture_time, &capture_interval_tv, &next_capture_time);
  }

  // Sleep till next capture time
#ifdef WITH_SLEEP
  struct timeval time_to_next_capture;
  (void) gettimeofday(&now, NULL);
  if (timercmp(&now, &next_capture_time, <)) {
    timersub(&next_capture_time, &now, &time_to_next_capture);

    uint64_t sleep_time =  ((uint64_t) time_to_next_capture.tv_sec) *
                            SEC_AS_USEC + time_to_next_capture.tv_usec;

    // Wake earlier to allow initialization
    if (sleep_time > WAKE_USEC_EARLY) {
      sleep_time -= WAKE_USEC_EARLY;
    } else {
      sleep_time = 0;
    }

    if (sleep_time >= MIN_SLEEP_TIME) {
      Serial.printf("Sleeping for %llu us\n", sleep_time);
      Serial.flush();

      // Preserve non-volatile data
      nv_data.next_capture_time = next_capture_time;

      camera_deinit();

      // Lock pin states (need to be unlocked at init again)
#ifdef WITH_FLASH
      rtc_gpio_hold_en(gpio_num_t(FLASH_GPIO_NUM));
#endif // WITH_FLASH
      rtc_gpio_hold_en(gpio_num_t(CAM_PWR_GPIO_NUM));
#if PWDN_GPIO_NUM >= 0
      rtc_gpio_hold_en(gpio_num_t(PWDN_GPIO_NUM)); //TODO: is this needed???
#endif // PWDN_GPIO_NUM >= 0

      esp_sleep_enable_timer_wakeup(sleep_time);
      esp_deep_sleep_start();
      // This line will never be reached....
    }
  }
#endif // WITH_SLEEP
}

void print_capability()
{
    Serial.print("Compiled options: ");
#ifdef WITH_FLASH
    Serial.print("WITH_FLASH ");
#endif
#ifdef WITH_SLEEP
    Serial.print("WITH_SLEEP ");
#endif
#ifdef WITH_CAM_PWDN
    Serial.print("WITH_CAM_PWDN ");
#endif
#ifdef WITH_EVIL_CAM_PWR_SHUTDOWN
    Serial.print("WITH_EVIL_CAM_PWR_SHUTDOWN ");
#endif
#ifdef WITH_SD_4BIT
    Serial.print("WITH_SD_4BIT ");
#endif
#ifdef WITH_SETUP_MODE_BUTTON
    Serial.print("WITH_SETUP_MODE_BUTTON ");
#endif
    Serial.println();
}
