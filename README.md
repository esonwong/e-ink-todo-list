# 墨水屏 Todo List

[![PlatformIO CI](https://github.com/esonwong/e-ink-todo-list/actions/workflows/tag.yml/badge.svg)](https://github.com/esonwong/e-ink-todo-list/actions/workflows/tag.yml)

使用[einktodo.com](https://einktodo.com)提供的 API，将待办事项显示在墨水屏上。

[English](README.en.md)

## 制作/使用教程

<https://blog.esonwong.com/e-ink-screen-todo-list-creation-turial>


## 按键说明

- 单击：如果当前处于配网模式则关闭配网；已连接 Wi-Fi 时立即刷新一次。
- 双击：通知服务器一次按键事件（`X-Device-Event: double-click`）。具体行为（默认是切换显示模式：todo / agent / image）由服务端配置，无需重刷固件。
- 长按 6 秒以上：清空配置 + 重启进入配网模式。

## TODO

- [ ] 支持 ESP8266 电池电压检测
- [ ] 配置界面风格
- [ ] 连接配置 AP 的二维码
- [x] 支持设置字体大小
- [x] 支持非 Todo List 模式
- [ ] 固定 API
- [x] CI/CD 自动发布固件
- [x] 自动更新固件
- [x] 自动更新 SSL/TLS 根证书
- [x] 启动界面
- [x] 记录上次检查更新的时间
- [x] 支持 ESP8266
- [x] 支持微雪第一代 7.5 寸墨水屏
- [x] 缓存待办事项



## Production

```sh
pio run --target erase --target upload --target monitor --environment production
```

## 网络兜底

设备默认优先访问主域名 API；当主入口不可达时，会自动按顺序尝试编译期配置的 HTTPS fallback 入口。

当前实现特性：

- 主域名继续使用 CertStore CA 校验
- fallback IP 使用证书指纹校验
- 自动记住最近一次成功的入口，并在下一次请求时优先尝试
- 显示内容拉取、资源更新和 OTA 更新共用同一套 fallback 逻辑

`platformio.ini` 中提供了 fallback 编译参数示例：

```ini
; -D FALLBACK_BASE_URL_1="https://107.173.82.8:16845"
; -D FALLBACK_TLS_FINGERPRINT_1="AA BB CC DD EE FF 00 11 22 33 44 55 66 77 88 99 AA BB CC DD"
```

实际测试时，可以在本地环境中覆盖为真实值，例如在 `local.ini` 的测试环境中配置 fallback 入口与证书指纹。

已验证结果：

- 主入口失败后，设备会继续请求 fallback HTTPS IP
- fallback 返回 200 后，设备可以正常下载 `certs.ar`
- 该行为已在真机上验证通过
