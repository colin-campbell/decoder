/*
    TheengsDecoder - Decode things and devices

    Copyright: (c)Florian ROBERT

    This file is part of TheengsDecoder.

    TheengsDecoder is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    TheengsDecoder is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "decoder.h"

#include <climits>
#include <string>

#include "devices.h"

#ifdef DEBUG_DECODER
#  include <stdio.h>
#  define DEBUG_PRINT(...) \
    { printf(__VA_ARGS__); }
#else
#  define DEBUG_PRINT(...) \
    {}
#endif

#ifdef UNIT_TESTING
#  define TEST_MAX_DOC 16384UL
#  include <assert.h>
static size_t peakDocSize = 0;
#endif

#define SVC_DATA "servicedata"
#define MFG_DATA "manufacturerdata"

typedef double (TheengsDecoder::*decoder_function)(const char* data_str,
                                                   int offset, int data_length,
                                                   bool reverse, bool canBeNegative, bool isFloat);

typedef double (TheengsDecoder::*staticbitdecoder_function)(const char* data_str,
                                                            const char* source_str, int offset, int bitindex,
                                                            const char* falseresult, const char* trueresult);

/*
 * @brief Revert the string data 2 by 2 to get the correct endianness
 */
void TheengsDecoder::reverse_hex_data(const char* in, char* out, int l) {
  int i = l, j = 0;
  while (i) {
    out[j] = in[i - 2];
    out[j + 1] = in[i - 1];
    i -= 2;
    j += 2;
  }
  out[l] = '\0';
}

double TheengsDecoder::bf_value_from_hex_string(const char* data_str,
                                                int offset, int data_length,
                                                bool reverse, bool canBeNegative, bool isFloat) {
  DEBUG_PRINT("extracting BCF data\n");

  long value = (long)value_from_hex_string(data_str, offset, data_length, reverse, false, false);
  double d_value = ((((value >> 8) * 100) + (uint8_t)value)) / 100.0;

  if (canBeNegative) {
    if (data_length == 4 && value > SHRT_MAX) {
      d_value = -d_value + (SCHAR_MAX + 1);
    }
  }

  return d_value;
}

/*
 * @brief Extracts the data value from the data string
 */
double TheengsDecoder::value_from_hex_string(const char* data_str,
                                             int offset, int data_length,
                                             bool reverse, bool canBeNegative, bool isFloat) {
  DEBUG_PRINT("offset: %d, len %d, rev %u, neg, %u, flo, %u\n",
              offset, data_length, reverse, canBeNegative, isFloat);
  std::string data(&data_str[offset], data_length);

  if (reverse) {
    reverse_hex_data(&data_str[offset], &data[0], data_length);
  }

  double value = 0;
  if (!isFloat) {
    value = strtoll(data.c_str(), NULL, 16);
    DEBUG_PRINT("extracted value from %s = %lld\n", data.c_str(), (long long)value);
  } else {
    union {
      long longV;
      float floatV;
    };
    longV = strtol(data.c_str(), NULL, 16);
    DEBUG_PRINT("extracted float value from %s = %f\n", data.c_str(), floatV);
    value = floatV;
  }

  if (canBeNegative) {
    if (data_length <= 2 && value > SCHAR_MAX) {
      value -= (UCHAR_MAX + 1);
    } else if (data_length == 4 && value > SHRT_MAX) {
      value -= (USHRT_MAX + 1);
    }
  }

  return value;
}

/*
 * @brief Removes the underscores at the beginning of key strings
 * when duplicate properties exist in a device.
 */
std::string TheengsDecoder::sanitizeJsonKey(const char* key_in) {
  unsigned int key_index = 0;
  while (key_in[key_index] == '_') {
    key_index++;
  }
  return std::string(key_in + key_index);
}

/*
 * @brief Checks to ensure accessing data at the index + length of the string is valid.
 */
bool TheengsDecoder::data_index_is_valid(const char* str, size_t index, size_t len) {
  if (strlen(str) < (index + len)) {
    return false;
  }
  return true;
}

bool TheengsDecoder::data_length_is_valid(size_t data_len, size_t default_min,
                                          const JsonArray& condition, int* idx) {
  std::string op = condition[*idx + 1].as<std::string>();
  if (!op.empty() && op.length() > 2) {
    return (data_len >= default_min);
  }

  if (!condition[*idx + 2].is<size_t>()) {
    *idx = -1;
    return false;
  }

  size_t req_len = condition[*idx + 2].as<size_t>();

  *idx += 2;
  return evaluateDatalength(op, data_len, req_len);
}

uint8_t TheengsDecoder::getBinaryData(char ch) {
  uint8_t data = 0;
  if (ch >= '0' && ch <= '9')
    data = ch - '0';
  else if (ch >= 'a' && ch <= 'f')
    data = 10 + (ch - 'a');

  return data;
}

bool TheengsDecoder::evaluateDatalength(std::string op, size_t data_len, size_t req_len) {
  if (op == "=" && data_len == req_len) return true;
  if (op == ">=" && data_len >= req_len) return true;
  if (op == ">" && data_len > req_len) return true;
  if (op == "<=" && data_len <= req_len) return true;
  if (op == "<" && data_len < req_len) return true;

  return false;
}

bool TheengsDecoder::checkDeviceMatch(const JsonArray& condition,
                                      const char* svc_data,
                                      const char* mfg_data,
                                      const char* dev_name,
                                      const char* svc_uuid,
                                      const char* mac_id) {
  bool match = false;
  int cond_size = condition.size();

  for (int i = 0; i < cond_size;) {
    if (condition[i].is<JsonArray>()) {
      DEBUG_PRINT("found nested array\n");
      match = checkDeviceMatch(condition[i], svc_data, mfg_data, dev_name, svc_uuid, mac_id);

      if (++i < cond_size) {
        if (!match && *condition[i].as<const char*>() == '|') {
        } else if (match && *condition[i].as<const char*>() == '&') {
          match = false;
        } else {
          break;
        }
        i++;
      } else {
        break;
      }
    }

    const char* cmp_str = nullptr;
    const char* cond_str = condition[i].as<const char*>();
    if (svc_data != nullptr && strstr(cond_str, SVC_DATA) != nullptr) {
      if (data_length_is_valid(strlen(svc_data), m_minSvcDataLen, condition, &i)) {
        cmp_str = svc_data;
        match = true;
      } else {
        match = false;
        if (i < 0) {
          break;
        }
      }
    } else if (mfg_data != nullptr && strstr(cond_str, MFG_DATA) != nullptr) {
      if (data_length_is_valid(strlen(mfg_data), m_minMfgDataLen, condition, &i)) {
        cmp_str = mfg_data;
        match = true;
      } else {
        match = false;
        if (i < 0) {
          break;
        }
      }
    } else if (mfg_data == nullptr && strstr(cond_str, "no-mfgdata") != nullptr) {
      match = true;
    } else if (dev_name != nullptr && strstr(cond_str, "name") != nullptr) {
      cmp_str = dev_name;
    } else if (svc_uuid != nullptr && strstr(cond_str, "uuid") != nullptr) {
      cmp_str = svc_uuid;
    } else {
      break;
    }

    if (!match && cmp_str == nullptr) {
      while (i < cond_size && *cond_str != '|') {
        if (!condition[++i].is<const char*>()) {
          continue;
        }
        cond_str = condition[i].as<const char*>();
      }

      if (i < cond_size && cond_str != nullptr) {
        i++;
        continue;
      }
    }

    cond_str = condition[++i].as<const char*>();
    if (cmp_str != nullptr && cond_str != nullptr && *cond_str != '&' && *cond_str != '|') {
      if (cmp_str == svc_uuid && !strncmp(cmp_str, "0x", 2)) {
        cmp_str += 2;
      }

      if (strstr(cond_str, "contain") != nullptr) {
        if (strstr(cmp_str, condition[++i].as<const char*>()) != nullptr) {
          match = true; // (strstr(cond_str, "not_") != nullptr) ? false : true;
        } else {
          match = false; // (strstr(cond_str, "not_") != nullptr) ? true : false;
        }
        i++;
      } else if (strstr(cond_str, "mac@index") != nullptr) {
        size_t cond_index = condition[++i].as<size_t>();
        size_t cond_len = 12;
        const char* string_to_compare = nullptr;
        std::string mac_string = mac_id;

        // remove colons and make lower case
        for (int x = 0; x < mac_string.length(); x++) {
          if (mac_string[x] == ':') {
            mac_string.erase(x, 1);
          }
          mac_string[x] = tolower(mac_string[x]);
        }

        string_to_compare = mac_string.c_str();

        if (strstr(cond_str, "revmac@index") != nullptr) {
          char* reverse_mac_string = (char*)malloc(strlen(string_to_compare) + 1);

          reverse_hex_data(string_to_compare, reverse_mac_string, 12);
          string_to_compare = reverse_mac_string;
        }

        if (!data_index_is_valid(cmp_str, cond_index, cond_len)) {
          DEBUG_PRINT("Invalid data %s; skipping\n", cmp_str);
          match = false;
          break;
        }

        DEBUG_PRINT("comparing value: %s to %s at index %zu\n",
                    &cmp_str[cond_index],
                    string_to_compare,
                    cond_index);

        if (strncmp(&cmp_str[cond_index],
                    string_to_compare,
                    12) == 0) {
          match = true;
        } else {
          match = false;
        }

        i++;
      } else if (strstr(cond_str, "index") != nullptr) {
        size_t cond_index = condition[++i].as<size_t>();
        size_t cond_len = strlen(condition[++i].as<const char*>());

        if (!data_index_is_valid(cmp_str, cond_index, cond_len)) {
          DEBUG_PRINT("Invalid data %s; skipping\n", cmp_str);
          match = false;
          break;
        }

        bool inverse = false;
        if (*condition[i].as<const char*>() == '!') {
          inverse = true;
          i++;
        }

        DEBUG_PRINT("comparing value: %s to %s at index %zu\n",
                    &cmp_str[cond_index],
                    condition[i].as<const char*>(),
                    cond_index);

        if (strncmp(&cmp_str[cond_index],
                    condition[i].as<const char*>(),
                    cond_len) == 0) {
          match = inverse ? false : true;
        } else {
          match = inverse ? true : false;
        }

        i++;
      }

      cond_str = condition[i].as<const char*>();
    }

    if (i < cond_size && cond_str != nullptr) {
      if (!match && *cond_str == '|') {
        i++;
        continue;
      } else if (match && *cond_str == '&') {
        i++;
        match = false;
        continue;
      } else if (match) { // check for AND case before exit
        while (i < cond_size && *cond_str != '&') {
          if (!condition[++i].is<const char*>()) {
            continue;
          }
          cond_str = condition[i].as<const char*>();
        }

        if (i < cond_size && cond_str != nullptr) {
          i++;
          match = false;
          continue;
        }
      }
    }
    break;
  }
  return match;
}

bool TheengsDecoder::checkPropCondition(const JsonArray& prop_condition,
                                        const char* svc_data,
                                        const char* mfg_data) {
  int cond_size = prop_condition.size();
  bool cond_met = prop_condition.isNull();

  if (!cond_met) {
    for (int i = 0; i < cond_size; i += 4) {
      if (prop_condition[i].is<JsonArray>()) {
        DEBUG_PRINT("found nested array\n");
        cond_met = checkPropCondition(prop_condition[i], svc_data, mfg_data);

        if (++i < cond_size) {
          if (!cond_met && *prop_condition[i].as<const char*>() == '|') {
          } else if (cond_met && *prop_condition[i].as<const char*>() == '&') {
            cond_met = false;
          } else {
            break;
          }
          i++;
        } else {
          break;
        }
      }

      bool inverse = 0;
      const char* prop_data_src = prop_condition[i];
      const char* data_src = nullptr;

      if (svc_data && strstr(prop_data_src, SVC_DATA) != nullptr) {
        data_src = svc_data;
      } else if (mfg_data && strstr(prop_data_src, MFG_DATA) != nullptr) {
        data_src = mfg_data;
      }

      if (data_src) {
        if (prop_condition[i + 1].is<int>()) {
          inverse = *(const char*)prop_condition[i + 2] == '!';
          size_t cond_len = strlen(prop_condition[i + 2 + inverse].as<const char*>());
          if (strstr((const char*)prop_condition[i + 2], "bit") != nullptr) {
            char ch = *(data_src + prop_condition[i + 1].as<int>());
            uint8_t data = getBinaryData(ch);

            uint8_t shift = prop_condition[i + 3].as<uint8_t>();
            uint8_t val = prop_condition[i + 4].as<uint8_t>();
            if (((data >> shift) & 0x01) == val) {
              cond_met = true;
            }
            i += 2;
          } else if (!strncmp(&data_src[prop_condition[i + 1].as<int>()],
                              prop_condition[i + 2 + inverse].as<const char*>(), cond_len)) {
            cond_met = inverse ? false : true;
          } else if (strncmp(&data_src[prop_condition[i + 1].as<int>()],
                             prop_condition[i + 2 + inverse].as<const char*>(), cond_len)) {
            cond_met = inverse ? true : false;
          }
        } else {
          std::string op = prop_condition[i + 1].as<std::string>();
          size_t data_len = strlen(data_src);
          size_t req_len = prop_condition[i + 2].as<size_t>();

          cond_met = evaluateDatalength(op, data_len, req_len);
        }
      } else {
        DEBUG_PRINT("ERROR property condition data source invalid\n");
        return false;
      }

      i += inverse;

      if (cond_size > (i + 3)) {
        if (!cond_met && *prop_condition[i + 3].as<const char*>() == '|') {
          continue;
        } else if (cond_met && *prop_condition[i + 3].as<const char*>() == '&') {
          cond_met = false;
          continue;
        } else {
          break;
        }
      }
    }
  }

  return cond_met;
}

/*
 * @brief Compares the input json values to the known devices and
 * decodes the data if a match is found.
 */
int TheengsDecoder::decodeBLEJson(JsonObject& jsondata) {
#ifdef UNIT_TESTING
  DynamicJsonDocument doc(TEST_MAX_DOC);
#else
  DynamicJsonDocument doc(m_docMax);
#endif
  const char* svc_data = jsondata[SVC_DATA].as<const char*>();
  const char* mfg_data = jsondata[MFG_DATA].as<const char*>();
  const char* dev_name = jsondata["name"].as<const char*>();
  const char* svc_uuid = jsondata["servicedatauuid"].as<const char*>();
  const char* mac_id = jsondata["id"].as<const char*>();
  int success = -1;

  // if there is no data to decode just return
  if (svc_data == nullptr && mfg_data == nullptr && dev_name == nullptr) {
    DEBUG_PRINT("Invalid data\n");
    return success;
  }

  /* loop through the devices and attempt to match the input data to a device parameter set */
  for (auto i_main = 0; i_main < sizeof(_devices) / sizeof(_devices[0]); ++i_main) {
    DeserializationError error = deserializeJson(doc, _devices[i_main][0]);
    if (error) {
      DEBUG_PRINT("deserializeJson() failed: %s\n", error.c_str());
#ifdef UNIT_TESTING
      assert(0);
#endif
      return success;
    }
#ifdef UNIT_TESTING
    if (doc.memoryUsage() > peakDocSize)
      peakDocSize = doc.memoryUsage();
#endif

    /* found a match, extract the data */
    JsonArray selectedCondition;
#ifdef NO_MAC_ADDR
    if (doc.containsKey("conditionnomac")) {
      selectedCondition = doc["conditionnomac"];
    } else {
      selectedCondition = doc["condition"];
    }
#else
    selectedCondition = doc["condition"];
#endif
    if (checkDeviceMatch(selectedCondition, svc_data, mfg_data, dev_name, svc_uuid, mac_id)) {
      jsondata["brand"] = doc["brand"];
      jsondata["model"] = doc["model"];
      jsondata["model_id"] = doc["model_id"];
      if (doc.containsKey("tag")) {
        doc.add("type");
        doc["type"] = NULL;

        std::string tagstring = doc["tag"];
        int type = strtol(tagstring.substr(0, 2).c_str(), NULL, 16);

        switch (type) {
          case 1:
            doc["type"] = "THB"; // Termperature, Humidity, Battery
            break;
          case 2:
            doc["type"] = "THBX"; // Termperature, Humidity, Battery, Extra
            break;
          case 3:
            doc["type"] = "BBQ"; // Multip probe temperatures only
            break;
          case 4:
            doc["type"] = "CTMO"; // Contact and/or Motion sensor
            break;
          case 5:
            doc["type"] = "SCALE"; // weight scale
            break;
          case 6:
            doc["type"] = "BCON"; // iBeacon protocol
            break;
          case 7:
            doc["type"] = "ACEL"; // acceleration
            break;
          case 8:
            doc["type"] = "BATT"; // battery
            break;
          case 9:
            doc["type"] = "PLANT"; // plant sensors
            break;
          case 10:
            doc["type"] = "TIRE"; // tire pressure monitoring system
            break;
          case 11:
            doc["type"] = "BODY"; // health monitoring devices
            break;
          case 12:
            doc["type"] = "ENRG"; // energy monitoring devices
            break;
          case 13:
            doc["type"] = "WCVR"; // window covering
            break;
          case 14:
            doc["type"] = "ACTR"; // ON/OFF actuators
            break;
          case 15:
            doc["type"] = "AIR"; // air environmental monitoring devices
            break;
          case 16:
            doc["type"] = "TRACK"; // Bluetooth tracker
            break;
          case 17:
            doc["type"] = "BTN"; // Button
            break;
          case 254:
            doc["type"] = "RMAC"; // random MAC address devices
            break;
          case 255:
            doc["type"] = "UNIQ"; // unique devices
            break;
        }

        if (!doc["type"].isNull()) {
          jsondata["type"] = doc["type"];
        } else {
          DEBUG_PRINT("ERROR - no valid device type present in model tag property\n");
        }

        // Octet Byte[1] bits[7-0] - True/False tags
        if (tagstring.length() >= 4) {
          // bits[3-0]
          uint8_t data = getBinaryData(tagstring[3]);

          if (((data >> 0) & 0x01) == 1) { // CIDC - NOT Company ID Compliant
            doc.add("cidc");
            doc["cidc"] = false;
            jsondata["cidc"] = doc["cidc"];
          }

          if (((data >> 1) & 0x01) == 1) { // Active Scanning required
            doc.add("acts");
            doc["acts"] = true;
            jsondata["acts"] = doc["acts"];
          }

          if (((data >> 2) & 0x01) == 1) { // Continuous Scanning required
            doc.add("cont");
            doc["cont"] = true;
            jsondata["cont"] = doc["cont"];
          }

          if (((data >> 3) & 0x01) == 1) { // Discoverable as Device Tracker
            doc.add("track");
            doc["track"] = true;
            jsondata["track"] = doc["track"];
          }

          // bits[7-4]
          data = getBinaryData(tagstring[2]);

          if (((data >> 0) & 0x01) == 1) { // PRMAC - Potential RMAC device - if not defined with Identity MAC and IRK in Theengs Gateway
            doc.add("prmac");
            doc["prmac"] = true;
            jsondata["prmac"] = doc["prmac"];
          }
        }

        // Octet Byte[2] - Encryption Model
        if (tagstring.length() >= 6) {
          int encrmode = strtol(tagstring.substr(4, 2).c_str(), NULL, 16);
          DEBUG_PRINT("encrmode: %d\n", encrmode);
          if (encrmode > 0) {
            doc.add("encr");
            doc["encr"] = encrmode;
            jsondata["encr"] = doc["encr"];
          }
        }
      }

      JsonObject properties = doc["properties"];

      /* Loop through all the devices properties and extract the values */
      for (JsonPair kv : properties) {
        JsonObject prop = kv.value().as<JsonObject>();

        if (checkPropCondition(prop["condition"], svc_data, mfg_data)) {
          JsonArray decoder = prop["decoder"];
          if (strstr((const char*)decoder[0], "value_from_hex_data") != nullptr) {
            const char* src = svc_data;
            if (strstr((const char*)decoder[1], MFG_DATA)) {
              src = mfg_data;
            }

            /* use a double for all values and cast later if required */
            double temp_val;
            static double cal_val = 0;
            std::string proc_str = "";

            if (data_index_is_valid(src, decoder[2].as<int>(), decoder[3].as<int>())) {
              decoder_function dec_fun = &TheengsDecoder::value_from_hex_string;

              if (strstr((const char*)decoder[0], "bf") != nullptr) {
                dec_fun = &TheengsDecoder::bf_value_from_hex_string;
              }

              temp_val = (this->*dec_fun)(src, decoder[2].as<int>(),
                                          decoder[3].as<int>(),
                                          decoder[4].as<bool>(),
                                          decoder[5].isNull() ? true : decoder[5].as<bool>(),
                                          decoder[6].isNull() ? false : decoder[6].as<bool>());

            } else {
              break;
            }

            /* Do any required post processing of the value */
            if (prop.containsKey("post_proc")) {
              JsonArray post_proc = prop["post_proc"];
              for (unsigned int i = 0; i < post_proc.size(); i += 2) {
                if (cal_val && post_proc[i + 1].as<const char*>() != NULL &&
                    strncmp(post_proc[i + 1].as<const char*>(), ".cal", 4) == 0) {
                  switch (*post_proc[i].as<const char*>()) {
                    case '/':
                      temp_val /= cal_val;
                      break;
                    case '*':
                      temp_val *= cal_val;
                      break;
                    case '-':
                      temp_val -= cal_val;
                      break;
                    case '+':
                      temp_val += cal_val;
                      break;
                  }
                } else {
                  if (strlen(post_proc[i].as<const char*>()) == 1) {
                    switch (*post_proc[i].as<const char*>()) {
                      case '/':
                        temp_val /= post_proc[i + 1].as<double>();
                        break;
                      case '*':
                        temp_val *= post_proc[i + 1].as<double>();
                        break;
                      case '-':
                        temp_val -= post_proc[i + 1].as<double>();
                        break;
                      case '+':
                        temp_val += post_proc[i + 1].as<double>();
                        break;
                      case '%': {
                        long val = (long)temp_val;
                        temp_val = val % post_proc[i + 1].as<long>();
                        break;
                      }
                      case '<': {
                        long val = (long)temp_val;
                        temp_val = val << post_proc[i + 1].as<unsigned int>();
                        break;
                      }
                      case '>': {
                        long val = (long)temp_val;
                        temp_val = val >> post_proc[i + 1].as<unsigned int>();
                        break;
                      }
                      case '!': {
                        bool val = (bool)temp_val;
                        temp_val = !val;
                        break;
                      }
                      case '&': {
                        long long val = (long long)temp_val;
                        temp_val = val & post_proc[i + 1].as<unsigned int>();
                        break;
                      }
                      case '^': {
                        long long val = (long long)temp_val;
                        temp_val = val ^ post_proc[i + 1].as<unsigned int>();
                        break;
                      }
                    }
                  } else if (strncmp(post_proc[i].as<const char*>(), "max", 3) == 0) {
                    if (temp_val > post_proc[i + 1].as<double>()) {
                      temp_val = post_proc[i + 1].as<double>();
                    }
                  } else if (strncmp(post_proc[i].as<const char*>(), "min", 3) == 0) {
                    if (temp_val < post_proc[i + 1].as<double>()) {
                      temp_val = post_proc[i + 1].as<double>();
                    }
                  } else if (strncmp(post_proc[i].as<const char*>(), "±", 1) == 0) {
                    if (temp_val < 0) {
                      temp_val += post_proc[i + 1].as<double>();
                    } else {
                      temp_val -= post_proc[i + 1].as<double>();
                    }
                  } else if (strncmp(post_proc[i].as<const char*>(), "abs", 3) == 0) {
                    long long val = (long long)temp_val;
                    temp_val = abs(val);
                  } else if (strncmp(post_proc[i].as<const char*>(), "SBBT-dir", 8) == 0) { // "SBBT" decoder specific post_proc
                    if (temp_val < 0) {
                      proc_str = "down";
                    } else if (temp_val > 0) {
                      proc_str = "up";
                    } else {
                      proc_str = "—";
                    }
                  }
                }
              }
            }

            /* If there is any underscores at the beginning of the property name, there is multiple
                * properties of this type, we need remove the underscores for creating the key.
                */
            std::string _key = sanitizeJsonKey(kv.key().c_str());

            /* calculation values extracted from data are not added to the decoded output
                * instead we store them temporarily to use with the next data properties.
                */
            if (_key == ".cal") {
              cal_val = temp_val;
              continue;
            }

            /* Cast to a different value type if specified */
            if (prop.containsKey("is_bool")) {
              jsondata[_key] = (bool)temp_val;
            } else {
              jsondata[_key] = temp_val;
            }

            /* _key as string if proc_str != "" */
            if (proc_str != "") {
              jsondata[_key] = proc_str;
            }

            /* If the property is temp in C, make sure to convert and add temp in F */
            if (_key.find("tempc", 0, 5) != std::string::npos) {
              double tc = jsondata[_key];
              _key[4] = 'f';
              jsondata[_key] = tc * 1.8 + 32;
              _key[4] = 'c';
            }

            /* If the property is tempf in F, make sure to convert and add temp in C */
            if (_key.find("tempf", 0, 5) != std::string::npos) {
              double tc = jsondata[_key];
              _key[4] = 'c';
              jsondata[_key] = (tc - 32) * 5 / 9;
              _key[4] = 'f';
            }

            /* If the property is with suffix _cm, make sure to convert and add length in inches */
            if (_key.find("_cm", _key.length() - 3, 3) != std::string::npos) {
              double tc = jsondata[_key];
              _key.replace(_key.length() - 3, 3, "_in");
              jsondata[_key] = tc / 2.54;
              _key.replace(_key.length() - 3, 3, "_cm");
            }

            success = i_main;
            DEBUG_PRINT("found value = %s : %.2f\n", _key.c_str(), jsondata[_key].as<double>());
          } else if (strstr((const char*)decoder[0], "static_value") != nullptr) {
            if (strstr((const char*)decoder[0], "bit") != nullptr) {
              JsonArray staticbitdecoder = prop["decoder"];
              const char* data_src = nullptr;

              if (svc_data && strstr((const char*)staticbitdecoder[1], SVC_DATA) != nullptr) {
                data_src = svc_data;
              } else if (mfg_data && strstr((const char*)staticbitdecoder[1], MFG_DATA) != nullptr) {
                data_src = mfg_data;
              }

              char ch = *(data_src + staticbitdecoder[2].as<int>());
              uint8_t data = getBinaryData(ch);
              uint8_t shift = staticbitdecoder[3].as<uint8_t>();
              int x = 4 + ((data >> shift) & 0x01);

              jsondata[sanitizeJsonKey(kv.key().c_str())] = staticbitdecoder[x];
              success = i_main;
            } else {
              jsondata[sanitizeJsonKey(kv.key().c_str())] = decoder[1];
              success = i_main;
            }
          } else if (strstr((const char*)decoder[0], "string_from_hex_data") != nullptr) {
            const char* src = svc_data;
            if (strstr((const char*)decoder[1], MFG_DATA)) {
              src = mfg_data;
            }

            std::string value(src + decoder[2].as<int>(), decoder[3].as<int>());

            /* Lookup table */
            if (prop.containsKey("lookup")) {
              JsonArray lookup = prop["lookup"];
              for (unsigned int i = 0; i < lookup.size(); i += 2) {
                if (lookup[i].as<std::string>() == value) {
                  if (lookup[i + 1].as<std::string>() != lookup[i + 1]) {
                    int valueint = lookup[i + 1].as<int>();
                    jsondata[sanitizeJsonKey(kv.key().c_str())] = valueint;
                  } else {
                    value = lookup[i + 1].as<std::string>();
                    jsondata[sanitizeJsonKey(kv.key().c_str())] = value;
                  }

                  success = i_main;
                  break;
                }
              }
            } else {
              jsondata[sanitizeJsonKey(kv.key().c_str())] = value;
              success = i_main;
            }
          } else if (strstr((const char*)decoder[0], "mac_from_hex_data") != nullptr) {
            const char* src = svc_data;
            if (strstr((const char*)decoder[1], MFG_DATA)) {
              src = mfg_data;
            }

            std::string value(src + decoder[2].as<int>(), 12);

            // reverse MAC
            if (strstr((const char*)decoder[0], "revmac_from_hex_data") != nullptr) {
              const char* mac_string = nullptr;
              mac_string = value.c_str();
              char* reverse_mac_string = (char*)malloc(strlen(mac_string) + 1);
              reverse_hex_data(mac_string, reverse_mac_string, 12);
              value = reverse_mac_string;
              free(reverse_mac_string);
            }

            // upper case MAC
            for (int x = 0; x <= 12; x++) {
              value[x] = toupper(value[x]);
            }

            // add colons
            for (int x = 2; x <= 14; x += 3) {
              value.insert(x, 1, ':');
            }

            jsondata[sanitizeJsonKey(kv.key().c_str())] = value;
            success = i_main;
          } else if (strstr((const char*)decoder[0], "ascii_from_hex_data") != nullptr) {
            const char* src = svc_data;
            if (strstr((const char*)decoder[1], MFG_DATA)) {
              src = mfg_data;
            }

            std::string value(src + decoder[2].as<int>(), decoder[3].as<int>());
            std::string ascii = "";

            for (size_t i = 0; i < value.length(); i += 2) {
              std::string part = value.substr(i, 2);
              char ch = stoul(part, nullptr, 16);

              ascii += ch;
            }

            if (ascii != "") {
              jsondata[sanitizeJsonKey(kv.key().c_str())] = ascii;
            }

            success = i_main;
          }
        }
      }
      return success;
    }
  }
  return success;
}

int TheengsDecoder::getTheengModel(JsonDocument& doc, const char* model_id) {
  int mid_len = strlen(model_id);

  for (auto i = 0; i < sizeof(_devices) / sizeof(_devices[0]); ++i) {
    DeserializationError error = deserializeJson(doc, _devices[i][0]);
    if (error) {
      DEBUG_PRINT("deserializeJson() failed: %s\n", error.c_str());
#ifdef UNIT_TESTING
      assert(0);
#endif
      break;
    }
#ifdef UNIT_TESTING
    if (doc.memoryUsage() > peakDocSize)
      peakDocSize = doc.memoryUsage();
#endif

    if (doc.containsKey("model_id")) {
      if (strlen(doc["model_id"].as<const char*>()) != mid_len) {
        continue;
      }
      if (!strncmp(model_id, doc["model_id"], mid_len)) {
        return i;
      }
    }
  }

  return -1;
}

std::string TheengsDecoder::getTheengProperties(int mod_index) {
  return (mod_index < 0 || mod_index >= BLE_ID_NUM::BLE_ID_MAX) ? "" : _devices[mod_index][1];
}

std::string TheengsDecoder::getTheengProperties(const char* model_id) {
#ifdef UNIT_TESTING
  DynamicJsonDocument doc(TEST_MAX_DOC);
#else
  DynamicJsonDocument doc(m_docMax);
#endif
  int mod_index = getTheengModel(doc, model_id);
  return (mod_index < 0 || mod_index >= BLE_ID_NUM::BLE_ID_MAX) ? "" : _devices[mod_index][1];
}

std::string TheengsDecoder::getTheengAttribute(int model_id, const char* attribute) {
#ifdef UNIT_TESTING
  DynamicJsonDocument doc(TEST_MAX_DOC);
#else
  DynamicJsonDocument doc(m_docMax);
#endif
  std::string ret_attr = "";
  if (model_id >= 0 && model_id < BLE_ID_NUM::BLE_ID_MAX) {
    DeserializationError error = deserializeJson(doc, _devices[model_id][0]);
    if (error) {
      DEBUG_PRINT("deserializeJson() failed: %s\n", error.c_str());
#ifdef UNIT_TESTING
      assert(0);
#endif
    } else if (!doc[attribute].isNull()) {
      ret_attr = doc[attribute].as<std::string>();
    }
  }

  return ret_attr;
}

std::string TheengsDecoder::getTheengAttribute(const char* model_id, const char* attribute) {
#ifdef UNIT_TESTING
  DynamicJsonDocument doc(TEST_MAX_DOC);
#else
  DynamicJsonDocument doc(m_docMax);
#endif
  int mod_index = getTheengModel(doc, model_id);

  if (mod_index >= 0 && !doc[attribute].isNull()) {
    return std::string(doc[attribute].as<std::string>());
  }
  return "";
}

void TheengsDecoder::setMinServiceDataLen(size_t len) {
  m_minSvcDataLen = len;
}

void TheengsDecoder::setMinManufacturerDataLen(size_t len) {
  m_minMfgDataLen = len;
}

#ifdef UNIT_TESTING
int TheengsDecoder::testDocMax() {
  if (peakDocSize > m_docMax) {
    DEBUG_PRINT("Error: peak doc size > max; peak: %lu, max: %lu\n", peakDocSize, m_docMax);
  }
  return m_docMax - peakDocSize;
}
#endif
