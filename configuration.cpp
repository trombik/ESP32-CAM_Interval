/**
 * configuration.cpp - Parse and store configuration options
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

#include <Arduino.h>

#include <stdlib.h>
#include <string.h>

#include "configuration.h"
#include "parse_kv_file.h"

static bool parse_int(const char *in, int *out);
static bool parse_bool(const char *in, bool *out);

Configuration cfg;

static const PROGMEM char * frame_size_strings[] = {
"96x96",
"160x120",
"176x144",
"240x176",
"240x240",
"320x240",
"400x296",
"480x320",
"640x480",
"800x600",
"1024x768",
"1280x720",
"1280x1024",
"1600x1200",
"1920x1080",
"720x1280",
"864x1536",
"2048x1536",
"2560x1440",
"2560x1600",
"1080x1920",
"2560x1920"
};
static const PROGMEM char * wb_mode_strings[] = {
"auto",
"sunny",
"cloudy",
"office",
"home"
};
static const PROGMEM char * special_effect_strings[] = {
"none",
"negative",
"grayscale",
"red tint",
"green tint",
"blue tint",
"sepia"
};

static int orientation_to_rotation(int orientation) {
  int rotation = 0;
  switch (orientation) {
  case 6:
    rotation = 90;
    break;
  case 3:
    rotation = 180;
    break;
  case 8:
    rotation = 270;
    break;
  case 1:
  default:
    rotation = 0;
    break;
  }
  return rotation;
}

/**
 * Parse base-10 interger string
 *
 * @returns true on success, false on error
 */
static bool parse_int(const char *in, int *out)
{
  char *endp;
    int tmp = strtol(in, &endp, 10);
    if (endp == NULL) {
      return false;
    }
    *out = tmp;
    return true;
}

/**
 * Parse boolean string
 *
 * @returns true on success, false on error
 */
static bool parse_bool(const char *in, bool *out)
{
    if (strcasecmp(in, "true") == 0 ||
        strcasecmp(in, "yes") == 0 ||
        strcmp(in, "1") == 0) {
      *out = true;
    } else if (strcasecmp(in, "false") == 0 ||
        strcasecmp(in, "no") == 0 ||
        strcmp(in, "0") == 0) {
      *out = false;
    } else {
      return false;
    }

    return true;
}

int Configuration::config_set(const char *key, const char *value)
{
  char *endp = NULL;

  Serial.printf(" - '%s' => '%s'", key, value);
  Serial.println();

  if (strcasecmp(key, "interval") == 0) {
    m_capture_interval = strtoul(value, &endp, 10);
    if (endp == NULL) {
      Serial.println("Value for 'interval' is not a valid integer number");
      return -2;
    }
    if (m_capture_interval < 1000) {
      // Date/Time filename format doesn't support intervals < 1 Second.
      Serial.println("Capture interval to small, changing to 1 Sec.");
      m_capture_interval = 1000;
    }
  } else if (strcasecmp(key, "ssid") == 0 ||
             strcasecmp(key, "password") == 0 ||
             strcasecmp(key, "ntp_server") == 0) {
    Serial.printf("WARNING: ignoring deprecated option '%s'\n", key);
  } else if (strcasecmp(key, "timezone") == 0) {
    if (strlen(value) > sizeof(m_tzinfo) - 1) {
      Serial.printf("Value of 'tzinfo' too long (>= %d byte)\n",
                        sizeof(m_tzinfo));
      return -2;
    }
    strcpy(m_tzinfo, value);
  } else if (strcasecmp(key, "rotation") == 0) {
    int int_value;
    if (parse_int(value, &int_value) != true) {
      Serial.printf("Value of '%s' is not a valid integer\n", key);
      return -2;
    }
    if (int_value == 0) { // 0 deg. rotation
      m_orientation = 1;
    } else if (int_value == 90 || int_value == -270) { // 90° CW / 270° CCW
      m_orientation = 6;
    } else if (int_value == 180 || int_value == -180) { // 180° CW/CCW
      m_orientation = 3;
    } else if (int_value == 270 || int_value == -90) { // 270° CW / 90° CCW
      m_orientation = 8;
    } else {
      Serial.printf("Value of '%s' is out of range\n", key);
      return -2;
    }
  } else if (strcasecmp(key, "enable_busy_led") == 0) {
    if (parse_bool(value, &(m_enable_busy_led)) != true) {
      Serial.printf("Value of '%s' is not a valid boolean\n", key);
      return -2;
    }
  } else if (strcasecmp(key, "enable_flash") == 0) {
    if (parse_bool(value, &(m_enable_flash)) != true) {
      Serial.printf("Value of '%s' is not a valid boolean\n", key);
      return -2;
    }
  } else if (strcasecmp(key, "training_shots") == 0) {
    int int_value;
    if (parse_int(value, &int_value) != true) {
      Serial.printf("Value of '%s' is not a valid integer\n", key);
      return -2;
    }
    if (int_value < 0) {
      Serial.printf("Value of '%s' is out of range\n", key);
      return -2;
    }
    m_training_shots = int_value;
  } else if(!strcasecmp(key, "framesize")) {
    if (strcasecmp(value, "QQVGA") == 0 ||
        strcasecmp(value, "160x120") == 0) {
      m_frame_size = FRAMESIZE_QQVGA;
    } else if (strcasecmp(value, "QCIF") == 0 ||
        strcasecmp(value, "176x144") == 0) {
      m_frame_size = FRAMESIZE_QCIF;
    } else if (strcasecmp(value, "HQVGA") == 0 ||
        strcasecmp(value, "240x176") == 0) {
      m_frame_size = FRAMESIZE_HQVGA;
    } else if (strcasecmp(value, "QVGA") == 0 ||
        strcasecmp(value, "320x240") == 0) {
      m_frame_size = FRAMESIZE_QVGA;
    } else if (strcasecmp(value, "CIF") == 0 ||
        strcasecmp(value, "400x296") == 0) {
      m_frame_size = FRAMESIZE_CIF;
    } else if (strcasecmp(value, "VGA") == 0 ||
        strcasecmp(value, "640x480") == 0) {
      m_frame_size = FRAMESIZE_VGA;
    } else if (strcasecmp(value, "SVGA") == 0 ||
        strcasecmp(value, "800x600") == 0) {
      m_frame_size = FRAMESIZE_SVGA;
    } else if (strcasecmp(value, "XGA") == 0 ||
        strcasecmp(value, "1024x768") == 0) {
      m_frame_size = FRAMESIZE_XGA;
    } else if (strcasecmp(value, "SXGA") == 0 ||
        strcasecmp(value, "1280x1024") == 0) {
      m_frame_size = FRAMESIZE_SXGA;
    } else if (strcasecmp(value, "UXGA") == 0 ||
        strcasecmp(value, "1600x1200") == 0) {
      m_frame_size = FRAMESIZE_UXGA;
    } else if (strcasecmp(value, "QXGA") == 0 ||
        strcasecmp(value, "2048x1536") == 0) {
      m_frame_size = FRAMESIZE_QXGA; // OV3660 only
    } else {
      Serial.printf("Invalid value for '%s'\n", key);
      return -2;
    }
  } else if(!strcasecmp(key, "quality")) {
    int int_value;
    if (parse_int(value, &int_value) != true) {
      Serial.printf("Value of '%s' is not a valid integer\n", key);
      return -2;
    }
    if (int_value < 10 || int_value > 63) {
      Serial.printf("Value of '%s' is out of range\n", key);
      return -2;
    }

    m_quality = int_value;
  } else if(!strcasecmp(key, "contrast")) {
    int int_value;
    if (parse_int(value, &int_value) != true) {
      Serial.printf("Value of '%s' is not a valid integer\n", key);
      return -2;
    }
    if (int_value < -2 || int_value > 2) {
      Serial.printf("Value of '%s' is out of range\n", key);
      return -2;
    }

    m_contrast = int_value;
  } else if(!strcasecmp(key, "brightness")) {
    int int_value;
    if (parse_int(value, &int_value) != true) {
      Serial.printf("Value of '%s' is not a valid integer\n", key);
      return -2;
    }
    if (int_value < -2 || int_value > 2) {
      Serial.printf("Value of '%s' is out of range\n", key);
      return -2;
    }

    m_brightness = int_value;
  } else if(!strcasecmp(key, "saturation")) {
    int int_value;
    if (parse_int(value, &int_value) != true) {
      Serial.printf("Value of '%s' is not a valid integer\n", key);
      return -2;
    }
    if (int_value < -2 || int_value > 2) {
      Serial.printf("Value of '%s' is out of range\n", key);
      return -2;
    }
    m_saturation = int_value;
  } else if(!strcasecmp(key, "colorbar")) {
    if (parse_bool(value, &m_colorbar) != true) {
      Serial.printf("Value of '%s' is not a valid boolean\n", key);
      return -2;
    }
  } else if(!strcasecmp(key, "hmirror")) {
    if (parse_bool(value, &m_hmirror) != true) {
      Serial.printf("Value of '%s' is not a valid boolean\n", key);
      return -2;
    }
  } else if(!strcasecmp(key, "vflip")) {
    if (parse_bool(value, &m_vflip) != true) {
      Serial.printf("Value of '%s' is not a valid boolean\n", key);
      return -2;
    }
  } else if(!strcasecmp(key, "awb")) {
    if (parse_bool(value, &m_awb) != true) {
      Serial.printf("Value of '%s' is not a valid boolean\n", key);
      return -2;
    }
  } else if(!strcasecmp(key, "awb_gain")) {
    if (parse_bool(value, &m_awb_gain) != true) {
      Serial.printf("Value of '%s' is not a valid boolean\n", key);
      return -2;
    }
  } else if(!strcasecmp(key, "wb_mode")) {
    if (strcasecmp(value, "auto") == 0) {
      m_wb_mode = WbModeAuto;
    } else if (strcasecmp(value, "sunny") == 0) {
      m_wb_mode = WbModeSunny;
    } else if (strcasecmp(value, "cloudy") == 0) {
      m_wb_mode = WbModeCloudy;
    } else if (strcasecmp(value, "office") == 0) {
      m_wb_mode = WbModeOffice;
    } else if (strcasecmp(value, "home") == 0) {
      m_wb_mode = WbModeHome;
    } else {
      Serial.printf("Invalid value for '%s'\n", key);
      return -2;
    }
  } else if(!strcasecmp(key, "agc")) {
    if (parse_bool(value, &m_agc) != true) {
      Serial.printf("Value of '%s' is not a valid boolean\n", key);
      return -2;
    }
  } else if(!strcasecmp(key, "agc_gain")) {
    int int_value;
    if (parse_int(value, &int_value) != true) {
      Serial.printf("Value of '%s' is not a valid integer\n", key);
      return -2;
    }
    if (int_value < 1 || int_value > 32) {
      Serial.printf("Value of '%s' is out of range\n", key);
      return -2;
    }
    m_agc_gain = int_value - 1;
  } else if(!strcasecmp(key, "gainceiling")) {
    int int_value;
    if (parse_int(value, &int_value) != true) {
      Serial.printf("Value of '%s' is not a valid integer\n", key);
      return -2;
    }
    if (int_value < 0 || int_value > 6) {
      Serial.printf("Value of '%s' is out of range\n", key);
      return -2;
    }
    m_gainceiling = (gainceiling_t) int_value;
  } else if(!strcasecmp(key, "aec")) {
    if (parse_bool(value, &m_aec) != true) {
      Serial.printf("Value of '%s' is not a valid boolean\n", key);
      return -2;
    }
  } else if(!strcasecmp(key, "aec_value")) {
    int int_value;
    if (parse_int(value, &int_value) != true) {
      Serial.printf("Value of '%s' is not a valid integer\n", key);
      return -2;
    }
    if (int_value < 0 || int_value > 1200) {
      Serial.printf("Value of '%s' is out of range\n", key);
      return -2;
    }
    m_aec_value = int_value;
  } else if(!strcasecmp(key, "aec2")) {
    if (parse_bool(value, &m_aec2) != true) {
      Serial.printf("Value of '%s' is not a valid boolean\n", key);
      return -2;
    }
  } else if(!strcasecmp(key, "ae_level")) {
    int int_value;
    if (parse_int(value, &int_value) != true) {
      Serial.printf("Value of '%s' is not a valid integer\n", key);
      return -2;
    }
    if (int_value < -2 || int_value > 2) {
      Serial.printf("Value of '%s' is out of range\n", key);
      return -2;
    }
    m_ae_level = int_value;
  } else if(!strcasecmp(key, "dcw")) {
    if (parse_bool(value, &m_dcw) != true) {
      Serial.printf("Value of '%s' is not a valid boolean\n", key);
      return -2;
    }
  } else if(!strcasecmp(key, "bpc")) {
    if (parse_bool(value, &m_bpc) != true) {
      Serial.printf("Value of '%s' is not a valid boolean\n", key);
      return -2;
    }
  } else if(!strcasecmp(key, "wpc")) {
    if (parse_bool(value, &m_wpc) != true) {
      Serial.printf("Value of '%s' is not a valid boolean\n", key);
      return -2;
    }
  } else if(!strcasecmp(key, "raw_gma")) {
    if (parse_bool(value, &m_raw_gma) != true) {
      Serial.printf("Value of '%s' is not a valid boolean\n", key);
      return -2;
    }
  } else if(!strcasecmp(key, "lenc")) {
    if (parse_bool(value, &m_lenc) != true) {
      Serial.printf("Value of '%s' is not a valid boolean\n", key);
      return -2;
    }
  } else if(!strcasecmp(key, "special_effect")) {
    if (strcasecmp(value, "none") == 0) {
      m_special_effect = SpecialEffectNone;
    } else if (strcasecmp(value, "negative") == 0) {
      m_special_effect = SpecialEffectNegative;
    } else if (strcasecmp(value, "grayscale") == 0) {
      m_special_effect = SpecialEffectGrayscale;
    } else if (strcasecmp(value, "red tint") == 0) {
      m_special_effect = SpecialEffectRedTint;
    } else if (strcasecmp(value, "green tint") == 0) {
      m_special_effect = SpecialEffectGreenTint;
    } else if (strcasecmp(value, "blue tint") == 0) {
      m_special_effect = SpecialEffectBlueTint;
    } else if (strcasecmp(value, "sepia") == 0) {
      m_special_effect = SpecialEffectSepia;
    } else {
      Serial.println("Invalid value for 'special_effect'");
      return -2;
    }
  } else {
    Serial.printf("Unknown key '%s', ignoring\n", key);
  }

  return 0;
}

String Configuration::configAsJSON() const
{
  String json;

  json += "{";
  json += "\"interval\": " + String(m_capture_interval);
  json += ",\"enable_busy_led\": " + String(m_enable_busy_led);
  json += ",\"enable_flash\": " + String(m_enable_flash);
  json += ",\"training_shots\": " + String(m_training_shots);
  json += ",\"timezone\": \"" + String(m_tzinfo) + '"';
  json += ",\"rotation\": " + String(orientation_to_rotation(m_orientation));
  json += ",\"framesize\": \"" + String(frame_size_strings[m_frame_size]) + '"';
  json += ",\"quality\": " + String(m_quality);
  json += ",\"contrast\": " + String(m_contrast);
  json += ",\"brightness\": " + String(m_brightness);
  json += ",\"saturation\": " + String(m_saturation);
  json += ",\"colorbar\": " + String(m_colorbar);
  json += ",\"hmirror\": " + String(m_hmirror);
  json += ",\"vflip\": " + String(m_vflip);
  json += ",\"awb\": " + String(m_awb);
  json += ",\"awb_gain\": " + String(m_awb_gain);
  json += ",\"wb_mode\": \"" + String(wb_mode_strings[m_wb_mode]) + '"';
  json += ",\"agc\": " + String(m_agc);
  json += ",\"agc_gain\": " + String(m_agc_gain+1);
  json += ",\"gainceiling\": " + String(m_gainceiling);
  json += ",\"aec\": " + String(m_aec);
  json += ",\"aec_value\": " + String(m_aec_value);
  json += ",\"aec2\": " + String(m_aec2);
  json += ",\"ae_level\": " + String(m_ae_level);
  json += ",\"dcw\": " + String(m_dcw);
  json += ",\"bpc\": " + String(m_bpc);
  json += ",\"wpc\": " + String(m_wpc);
  json += ",\"raw_gma\": " + String(m_raw_gma);
  json += ",\"lenc\": " + String(m_lenc);
  json += ",\"special_effect\": \"" + String(special_effect_strings[m_special_effect]) + '"';
  json += "}";

  return json;
}

static int config_set_wrapper(const char *key, const char *value) {
  return cfg.config_set(key, value);
}

bool Configuration::loadConfig()
{
  FILE *file = fopen(CONFIG_PATH, "r");
  if (file != NULL)  {
    Serial.println("Loading config... ");

    int err = parse_kv_file(file, &config_set_wrapper);

    fclose(file);

    if (err != 0) {
      Serial.printf("Failed to parse configuration, Error %d\n", err);
      return false;
    } else {
      Serial.println("Config loaded.");
    }
  } else {
    Serial.println("No config found, using defaults.");
  }

  return true;
}

bool Configuration::saveConfig()
{
  // TODO: switch from bool return to exceptions?
  // TODO: backup old config

  FILE *file = fopen(CONFIG_PATH, "w");
  if (file != NULL)  {
    Serial.println("Saving config... ");

    // TODO: Only write values that differ from default
    fputs("# ESP32-CAM interval - Configuration file\n", file);
    fputs("# Configuration Generated from Set-up mode\n", file);
    fputs("interval = ", file); fputs(String(m_capture_interval).c_str(), file); fputc('\n', file);
    fputs("enable_busy_led = ", file); fputs(String(m_enable_busy_led).c_str(), file); fputc('\n', file);
    fputs("enable_flash = ", file); fputs(String(m_enable_flash).c_str(), file); fputc('\n', file);
    fputs("training_shots = ", file); fputs(String(m_training_shots).c_str(), file); fputc('\n', file);
    fputs("timezone = ", file); fputs(m_tzinfo, file); fputc('\n', file);
    fputs("rotation = ", file); fputs(String(orientation_to_rotation(m_orientation)).c_str(), file); fputc('\n', file);
    fputs("framesize = ", file); fputs(frame_size_strings[m_frame_size], file); fputc('\n', file);
    fputs("quality = ", file); fputs(String(m_quality).c_str(), file); fputc('\n', file);
    fputs("contrast = ", file); fputs(String(m_contrast).c_str(), file); fputc('\n', file);
    fputs("brightness = ", file); fputs(String(m_brightness).c_str(), file); fputc('\n', file);
    fputs("saturation = ", file); fputs(String(m_saturation).c_str(), file); fputc('\n', file);
    fputs("colorbar = ", file); fputs(String(m_colorbar).c_str(), file); fputc('\n', file);
    fputs("hmirror = ", file); fputs(String(m_hmirror).c_str(), file); fputc('\n', file);
    fputs("vflip = ", file); fputs(String(m_vflip).c_str(), file); fputc('\n', file);
    fputs("awb = ", file); fputs(String(m_awb).c_str(), file); fputc('\n', file);
    fputs("awb_gain = ", file); fputs(String(m_awb_gain).c_str(), file); fputc('\n', file);
    fputs("wb_mode = ", file); fputs(wb_mode_strings[m_wb_mode], file); fputc('\n', file);
    fputs("agc = ", file); fputs(String(m_agc).c_str(), file); fputc('\n', file);
    fputs("agc_gain = ", file); fputs(String(m_agc_gain+1).c_str(), file); fputc('\n', file);
    fputs("gainceiling = ", file); fputs(String(m_gainceiling).c_str(), file); fputc('\n', file);
    fputs("aec = ", file); fputs(String(m_aec).c_str(), file); fputc('\n', file);
    fputs("aec_value = ", file); fputs(String(m_aec_value).c_str(), file); fputc('\n', file);
    fputs("aec2 = ", file); fputs(String(m_aec2).c_str(), file); fputc('\n', file);
    fputs("ae_level = ", file); fputs(String(m_ae_level).c_str(), file); fputc('\n', file);
    fputs("dcw = ", file); fputs(String(m_dcw).c_str(), file); fputc('\n', file);
    fputs("bpc = ", file); fputs(String(m_bpc).c_str(), file); fputc('\n', file);
    fputs("wpc = ", file); fputs(String(m_wpc).c_str(), file); fputc('\n', file);
    fputs("raw_gma = ", file); fputs(String(m_raw_gma).c_str(), file); fputc('\n', file);
    fputs("lenc = ", file); fputs(String(m_lenc).c_str(), file); fputc('\n', file);
    fputs("special_effect = ", file); fputs(special_effect_strings[m_special_effect], file); fputc('\n', file);

    fclose(file);
  } else {
    Serial.println("Unable to open config file for writing");
    return false;
  }

  return true;
}
