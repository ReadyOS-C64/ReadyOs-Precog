# readyos easyflash

- attach `readyos_easyflash.crt` as an easyflash cartridge
- mount `readyos_data.d64` on drive `8`
- enable reu `16mb`

## vice example

```sh
x64sc -reu -reusize 16384 -cartcrt readyos_easyflash.crt -drive8type 1541 -devicebackend8 0 +busdevice8 -8 readyos_data.d64
```
