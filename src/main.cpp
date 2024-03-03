#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "todoList.h"
#include "config.h"
#include "network.h"
#include "led.h"
#include "display.h"
#include "clock.h"

int updating = false;

void setup()
{

  Serial.begin(115200);
  Serial.println();
  Serial.println("Steup Start");
  initWifiWithManager();
  setClock();

  delay(1000);

  updating = true;
  downloadAndDrawTodo();
  updating = false;
}

void loop()
{
  // 每 1 分钟检查一次是否需要更新 todo
  time_t now = time(nullptr);
  if (now % 60 == 0 && !updating)
  {
    updating = true;
    downloadAndDrawTodo();
    updating = false;
  }

  // wait input from Serial
  String input = Serial.readStringUntil('\n');
  if (input.length() > 0)
  {
    Serial.print("input: ");
    Serial.println(input);
    char *inputChar = (char *)malloc(input.length() + 1);

    // 用 : 分割字符串
    input.toCharArray(inputChar, input.length() + 1);
    char *command = strtok(inputChar, ":");
    Serial.print("command: ");
    Serial.println(command);

    if (strcmp(command, "fr") == 0)
    {
      Serial.println("fr:强制刷新 todo");
      removeStore(savedTodoLastModifiedKey);
      updating = true;
      downloadAndDrawTodo();
      updating = false;
    }

    free(inputChar);
  }
}