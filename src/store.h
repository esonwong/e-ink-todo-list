#ifndef STORE_H
#define STORE_H
#include <EEPROM.h>

const char *storeName = "einktodo.com";

struct Setting
{
  char apiKey[41] = "";
  char apiUrl[200] = DEFAULT_API_URL;
};

const int settingSavedFlag = 0x1234;

struct RunningValue
{
  char todoLastModified[30] = "";
  // last check time
  time_t lastCheck = 0;
};
const int runningValueSavedFlag = 0x5678;

void saveSetting(Setting setting)
{
  EEPROM.begin(512);
  EEPROM.put(0, settingSavedFlag);
  EEPROM.put(sizeof(int), setting);
  EEPROM.commit();
  EEPROM.end();
}

Setting loadSetting()
{
  Setting setting;
  EEPROM.begin(512);
  int flag;
  EEPROM.get(0, flag);
  if (flag != settingSavedFlag)
  {
    EEPROM.end();
    return setting;
  }
  EEPROM.get(sizeof(int), setting);
  EEPROM.end();
  return setting;
}

void removeSetting()
{
  EEPROM.begin(512);
  EEPROM.put(0, 0);
  EEPROM.commit();
  EEPROM.end();
}

void saveRunningValue(RunningValue runningValue)
{
  EEPROM.begin(512);
  EEPROM.put(sizeof(int) + sizeof(Setting), runningValueSavedFlag);
  EEPROM.put(sizeof(int) + sizeof(Setting) + sizeof(int), runningValue);
  EEPROM.commit();
  EEPROM.end();
}

RunningValue loadRunningValue()
{
  RunningValue runningValue;
  EEPROM.begin(512);
  int flag;
  EEPROM.get(sizeof(int) + sizeof(Setting), flag);
  if (flag != runningValueSavedFlag)
  {
    EEPROM.end();
    return runningValue;
  }
  EEPROM.get(sizeof(int) + sizeof(Setting) + sizeof(int), runningValue);
  EEPROM.end();
  return runningValue;
}

Setting setting;
RunningValue runningValue;

void removeRunningValue()
{
  EEPROM.begin(512);
  EEPROM.put(sizeof(int) + sizeof(Setting), 0);
  EEPROM.commit();
  EEPROM.end();
  runningValue = loadRunningValue();
}

void initStore()
{
  setting = loadSetting();
  runningValue = loadRunningValue();
}

#endif // STORE_H