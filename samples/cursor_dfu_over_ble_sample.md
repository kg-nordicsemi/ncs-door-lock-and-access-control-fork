# DFU over BLE sample
_Exported on 7/29/2026 at 23:24:35 GMT+2 from Cursor (3.2.11)_

---

**User**

czesc mam pytanie czy jestes w stanie mi napisac minimalny sample który umozliwi mi wykonywanie dfu over ble smp na platformie nrf54lm20b, uzywam windows oraz smpmgr



---

**Cursor**

Cześć! Tak — mogę przygotować minimalny, samodzielny sample DFU przez BLE SMP dla:

- nRF Connect SDK 3.3.0
- `nrf54lm20dk/nrf54lm20b/cpuapp`
- MCUboot
- Windows + `smpmgr`
- generowanie podpisanego obrazu aktualizacyjnego
- komendy do wgrania, testowania i zatwierdzania firmware

W repozytorium jest już konfiguracja DFU SMP, którą można wykorzystać jako punkt odniesienia. Sample może ograniczać się do prostego licznika wersji wypisywanego przez UART, żeby łatwo potwierdzić aktualizację.

Mogę od razu utworzyć go w katalogu `samples/dfu_smp_ble/` wraz z instrukcją budowania i obsługi na Windows. Potrzebuję tylko potwierdzenia, czy używasz DK `nrf54lm20dk` z wariantem SoC `nrf54lm20b`.

---

**User**

zrób to

---

**Cursor**

Sample jest już utworzony. Teraz sprawdzam pełny build z MCUboot dla dokładnego targetu LM20B; w razie problemów z Kconfig poprawię konfigurację.

Gotowe. Sample znajduje się w:

`samples/dfu_smp_ble/`

Zawiera:

- BLE SMP/MCUmgr
- MCUboot i dwa sloty
- target `nrf54lm20dk/nrf54lm20b/cpuapp`
- wersjonowanie firmware
- instrukcję Windows + `smpmgr`
- bezpieczny przebieg upload → test → reset → confirm

Build został zweryfikowany pomyślnie. Obraz DFU:

`build/dfu_smp_ble/dfu_smp_ble/zephyr/zephyr.signed.bin`

Pełna instrukcja znajduje się w `samples/dfu_smp_ble/README.md`.

Uwaga: używany jest domyślny deweloperski klucz MCUboot — odpowiedni do testów, nie do produkcji.

---

**User**

okej mozesz wgrac ten sample ?

---

**Cursor**

Wgrane pomyślnie na DK o numerze `1051819740`.

Zaprogramowano i zweryfikowano `merged.hex`, następnie wykonano reset. Urządzenie powinno reklamować się jako:

`nRF54LM20B SMP`

---

**User**

okej zbuduj teraz aplke z podbita wersja  np v1.0.1 i wytlaumcz jak wykorzystac smpmgr do aktuazacji over ble

---

**Cursor**

Podbiłem wersję do `1.0.1`. Buduję ją w osobnym katalogu, żeby zachować wgrany obraz `1.0.0` jako bazę testu DFU.

Zbudowane. Obraz ma potwierdzoną wersję `1.0.1+0`.

Plik do aktualizacji:

`build/dfu_smp_ble_v1_0_1/dfu_smp_ble/zephyr/zephyr.signed.bin`

Na urządzeniu pozostawiłem `1.0.0`, aby można było wykonać prawdziwy test DFU.

## Aktualizacja w Windows

Skopiuj `zephyr.signed.bin` na Windows, następnie w PowerShell:

1. Sprawdź połączenie:

```powershell
smpmgr --timeout 10 --ble "nRF54LM20B SMP" image state-read
```

2. Prześlij obraz:

```powershell
smpmgr --timeout 40 --ble "nRF54LM20B SMP" image upload `
  "C:\firmware\zephyr.signed.bin"
```

Nie podawaj `--slot`; domyślnie obraz trafi do slotu aktualizacji.

3. Odczytaj hash obrazu w `slot=1`:

```powershell
smpmgr --timeout 10 --ble "nRF54LM20B SMP" image state-read
```

4. Skopiuj pełny hash SHA-256 slotu 1 i oznacz obraz do testowego uruchomienia:

```powershell
smpmgr --timeout 10 --ble "nRF54LM20B SMP" image state-write TUTAJ_HASH
```

5. Zresetuj urządzenie:

```powershell
smpmgr --timeout 10 --ble "nRF54LM20B SMP" os reset
```

Po restarcie MCUboot uruchomi wersję `1.0.1`.

6. Połącz się ponownie i zatwierdź aktualizację:

```powershell
smpmgr --timeout 10 --ble "nRF54LM20B SMP" image state-write --confirm
```

Bez ostatniej komendy MCUboot przy kolejnym restarcie wycofa aktualizację do `1.0.0`.

