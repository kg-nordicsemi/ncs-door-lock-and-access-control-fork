# Minimalny DFU przez BLE SMP — nRF54LM20B

Sample uruchamia standardową usługę MCUmgr SMP przez Bluetooth LE i używa
MCUboot do bezpiecznej zamiany obrazu. Jest przeznaczony dla:

- nRF Connect SDK 3.3.0,
- `nrf54lm20dk/nrf54lm20b/cpuapp`,
- Windows i `smpmgr`.

Urządzenie reklamuje się jako `nRF54LM20B SMP`.

> To jest konfiguracja deweloperska. Usługa SMP nie wymaga parowania ani
> uwierzytelnienia. Nie należy używać jej bez zmian w produkcie.

## 1. Budowanie i pierwsze programowanie

Uruchom z katalogu głównego workspace NCS, na przykład
`C:\ncs\v3.3.0` w terminalu nRF Connect SDK:

```powershell
west build -p always --sysbuild `
  -b nrf54lm20dk/nrf54lm20b/cpuapp `
  ncs-door-lock-and-access-control/samples/dfu_smp_ble `
  -d build/dfu_smp_ble_v1

west flash -d build/dfu_smp_ble_v1
```

Pierwsze programowanie zapisuje MCUboot i aplikację. W logu UART powinny
pojawić się wersja `1.0.0` i informacja o rozpoczęciu reklamowania.

## 2. Zbudowanie obrazu aktualizacji

Zmień `VERSION`, na przykład ustaw `PATCHLEVEL = 1`, a następnie:

```powershell
west build -p always --sysbuild `
  -b nrf54lm20dk/nrf54lm20b/cpuapp `
  ncs-door-lock-and-access-control/samples/dfu_smp_ble `
  -d build/dfu_smp_ble_v1_0_1
```

Plik przesyłany przez SMP:

```text
build\dfu_smp_ble_v1_0_1\dfu_smp_ble\zephyr\zephyr.signed.bin
```

Nie przesyłaj `merged.hex`: zawiera on także bootloader i jest przeznaczony do
programowania przewodowego.

## 3. Instalacja smpmgr na Windows

Można pobrać przenośny plik wykonywalny z:

https://github.com/intercreate/smpmgr/releases/latest

Alternatywnie, mając Python i `pipx`:

```powershell
pipx install smpmgr
smpmgr --version
```

## 4. Aktualizacja przez BLE

Poniższa sekwencja jawnie wykonuje upload, test i potwierdzenie. Jest
bezpieczniejsza niż natychmiastowe oznaczenie nowego obrazu jako permanentny.

Sprawdź połączenie:

```powershell
smpmgr --timeout 10 --ble "nRF54LM20B SMP" image state-read
```

Prześlij podpisany obraz do domyślnego slotu aktualizacji:

```powershell
smpmgr --timeout 40 --ble "nRF54LM20B SMP" image upload `
  "C:\ncs\v3.3.0\build\dfu_smp_ble_v1_0_1\dfu_smp_ble\zephyr\zephyr.signed.bin"
```

Odczytaj stan obrazów:

```powershell
smpmgr --timeout 10 --ble "nRF54LM20B SMP" image state-read
```

Skopiuj pełny hash SHA-256 obrazu ze `slot=1`, oznacz go do jednorazowego
uruchomienia i wykonaj reset:

```powershell
smpmgr --timeout 10 --ble "nRF54LM20B SMP" image state-write HASH_SLOTU_1
smpmgr --timeout 10 --ble "nRF54LM20B SMP" os reset
```

Po restarcie połącz się ponownie, sprawdź w UART wersję `1.0.1`, a następnie
potwierdź aktualnie działający obraz:

```powershell
smpmgr --timeout 10 --ble "nRF54LM20B SMP" image state-write --confirm
```

Jeżeli obraz nie zostanie potwierdzony, MCUboot przy kolejnym restarcie wróci
do poprzedniej wersji.

## Najczęstsze problemy

- Gdy Windows nie znajduje nazwy urządzenia, usuń je z pamięci Bluetooth,
  przeskanuj ponownie albo użyj adresu BLE zamiast nazwy w `--ble`.
- Pierwszy pakiet może trwać dłużej podczas kasowania flash; dlatego upload
  używa `--timeout 40`.
- Nowy obraz musi być zbudowany z tym samym kluczem MCUboot co obraz
  zaprogramowany przewodowo. Domyślny klucz deweloperski NCS nie jest
  przeznaczony do produkcji.
- Po zmianie układu partycji należy ponownie zaprogramować pełny
  `merged.hex`; takiej zmiany nie należy wykonywać samym DFU.
