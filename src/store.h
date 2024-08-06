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
  time_t lastCheck = 0;
};

Setting setting;
RunningValue runningValue;

void saveSetting(Setting setting = setting)
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
  Serial.println("Save setting done");
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

template <typename T>
void savePersistentValue(String key, T value)
{
  if (!LittleFS.begin())
  {
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }

  File file = LittleFS.open("/persistentValue.json", "r+");
  if (!file)
  {
    file = LittleFS.open("/persistentValue.json", "w+");
    if (!file)
    {
      Serial.println("Failed to open file for writing");
      LittleFS.end();
      return;
    }
  }

  JsonDocument doc;

  if (file.size() > 0)
  {
    DeserializationError error = deserializeJson(doc, file);
    if (error)
    {
      Serial.println("Failed to read file, using empty JSON object");
    }
  }

  doc[key] = value;

  file.seek(0);

  file.truncate(0);

  if (serializeJson(doc, file) == 0)
  {
    Serial.println("Failed to write to file");
  }

  // 关闭文件
  file.close();

  Serial.print("Save persistent value ");
  Serial.print(key);
  Serial.print(" = ");
  Serial.println(value);
  LittleFS.end();

#ifdef DEBUG_PERSISTENT_STORAGE
  // print file content
  LittleFS.begin();
  File readFile = LittleFS.open("/persistentValue.json", "r");
  Serial.println("File content:");
  while (readFile.available())
  {
    Serial.write(readFile.read());
  }
  Serial.println();
  readFile.close();
  LittleFS.end();
#endif
}

template <typename T>
T getPersistentValue(String key, T defaultValue)
{
  T value = defaultValue;
  if (!LittleFS.begin())
  {
    Serial.println("An Error has occurred while mounting LittleFS");
    return value;
  }
  File file = LittleFS.open("/persistentValue.json", "r");
  if (!file)
  {
    Serial.println("Failed to open file for reading");
    return value;
  }

  JsonDocument persistentValueJson;
  DeserializationError error = deserializeJson(persistentValueJson, file);
  if (error)
  {
    Serial.println("Failed to read file");
    file.close();
    return value;
  }

  value = persistentValueJson[key] | defaultValue;

  file.close();
  LittleFS.end();
  return value;
}

RunningValue loadRunningValue()
{
  return RunningValue();
}

void cleanRunningValue()
{
  runningValue = RunningValue();
}

void formatLittleFS()
{
  if (!LittleFS.begin())
  {
    Serial.println("An Error has occurred while mounting LittleFS");
    Serial.println("Format LittleFS");
    LittleFS.format();
    Serial.println("Format LittleFS done");
  }
  LittleFS.end();
}

void initStore()
{

  formatLittleFS();

  Serial.println("Init store");
  setting = loadSetting();
  runningValue = loadRunningValue();

#ifdef API_KEY
  // For debugging
  strcpy(setting.apiKey, API_KEY);
#endif
}

#endif // STORE_H