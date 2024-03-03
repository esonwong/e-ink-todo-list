#ifndef STORE_H
#define STORE_H

#include <Preferences.h>

Preferences preferences;

const char *storeName = "einktodo.com";

void saveToStore(const char *key, const char *value)
{
  // chekck key's length
  if (strlen(key) > 15)
  {
    Serial.println("Key is too long");
    return;
  }

  preferences.begin(storeName, false);
  preferences.putString(key, value);
  preferences.end();
}

const String getFromStore(const char *key)
{
  preferences.begin(storeName, true);
  String value = preferences.getString(key, "");
  preferences.end();
  return value;
}

void removeStore(const char *key)
{
  preferences.begin(storeName, false);
  preferences.remove(key);
  preferences.end();
}

#endif // STORE_H