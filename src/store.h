#ifndef STORE_H
#define STORE_H
#include <LittleFS.h>
#include <ArduinoJson.h>

struct Setting
{
  char apiKey[41] = "";
  char apiUrl[200] = DEFAULT_API_URL;
};

struct RunningValue
{
  char todoLastModified[30] = "";
  // last check time
  time_t lastCheck = 0;
};

Setting setting;
RunningValue runningValue;

void saveSetting(Setting setting)
{
  Serial.println("Save setting");
  if (!LittleFS.begin())
  {
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }
  File file = LittleFS.open("/setting.json", "w");
  if (!file)
  {
    Serial.println("Failed to open file for writing");
    return;
  }

  JsonDocument settingJson;
  settingJson["apiKey"] = setting.apiKey;
  settingJson["apiUrl"] = setting.apiUrl;

  serializeJson(settingJson, file);
  file.close();
  LittleFS.end();
}

Setting loadSetting()
{
  Serial.println("Load setting");
  Setting setting;
  if (!LittleFS.begin())
  {
    Serial.println("An Error has occurred while mounting LittleFS");
    return setting;
  }
  File file = LittleFS.open("/setting.json", "r");
  if (!file)
  {
    Serial.println("Failed to open file for reading");
    return setting;
  }

  JsonDocument settingJson;
  DeserializationError error = deserializeJson(settingJson, file);
  if (error)
  {
    Serial.println("Failed to read file");
    file.close();
    return setting;
  }

  strlcpy(setting.apiKey, settingJson["apiKey"] | "", sizeof(setting.apiKey));
  strlcpy(setting.apiUrl, settingJson["apiUrl"] | DEFAULT_API_URL, sizeof(setting.apiUrl));

  file.close();
  LittleFS.end();
  return setting;
}

void removeSetting()
{
  Serial.println("Remove setting");
  if (!LittleFS.begin())
  {
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }
  LittleFS.remove("/setting.json");
  LittleFS.end();
}

void saveRunningValue(RunningValue runningValue)
{
  Serial.println("Save running value");
  if (!LittleFS.begin())
  {
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }
  File file = LittleFS.open("/runningValue.json", "w");
  if (!file)
  {
    Serial.println("Failed to open file for writing");
    return;
  }

  JsonDocument runningValueJson;
  runningValueJson["todoLastModified"] = runningValue.todoLastModified;
  runningValueJson["lastCheck"] = runningValue.lastCheck;

  serializeJson(runningValueJson, file);
  file.close();
  LittleFS.end();
}

RunningValue loadRunningValue()
{
  return RunningValue();
}

void cleanRunningValue()
{
  runningValue = RunningValue();
}

void initStore()
{
  Serial.println("Init store");
  setting = loadSetting();
#ifdef API_KEY
  // For debugging
  strcpy(setting.apiKey, API_KEY);
#endif
  runningValue = loadRunningValue();
}

#endif // STORE_H