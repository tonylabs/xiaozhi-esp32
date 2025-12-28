---
description: "This rule provides hardware specifications and component logic"
alwaysApply: false
---

# Stikadoo AI Printer

This is the firmware of an AI printer powered by ESP32-P4 with a 480x480 RGB display. User can press a tactile switch button to trigger voice recording via I2S and streaming to a Speech to Text cloud service like Azure Cognitive.

## FEATURES

- A physical tactile switch button is attached to the `GPIO_NUM_24`, this button is the unique user interaction method.
- A 480*480 pixels QSPI display controlled by LVGL library.
- A `ES8311` audio codec connected to ESP32-P4 via I2C. This codec chip has a speaker and microphone connected.

## WORKFLOW

This whole project should be managed by different events, each event should show a LVGL screen owned by the following stages:

```c
#define MAIN_EVENT_SCHEDULE             (1 << 0)
#define MAIN_EVENT_SEND_AUDIO           (1 << 1)
#define MAIN_EVENT_ERROR                (1 << 4)
#define MAIN_EVENT_ACTIVATION_DONE      (1 << 5)
#define MAIN_EVENT_NETWORK_CONNECTED    (1 << 7)
#define MAIN_EVENT_NETWORK_DISCONNECTED (1 << 8)
#define MAIN_EVENT_TOGGLE_CHAT          (1 << 9)
#define MAIN_EVENT_START_LISTENING      (1 << 10)
#define MAIN_EVENT_STOP_LISTENING       (1 << 11)
#define MAIN_EVENT_STATE_CHANGED        (1 << 12)
```

### Initial Setup

1.	After power-on, the device performs a self-check. If Wi-Fi has not been configured (may use ESP32 preferences feature to store a status or a better way to identify whether end user configured the home WiFi credential before), it enter SoftAP provisioning mode and displays a QR code with heading and description text (the QR code should include hardware information, such as a unique identifier).
2.	The user scans the QR code via a mobile phone.
3.	The user selects the current Wi-Fi network and enters the Wi-Fi password.
4.	Stickerbox displays “Connecting to WiFi…”.
5.	After a successful connection, a four-digit verification code is displayed. The user enters this code on the phone and links the device.
6.	The device displays “Press to talk”, guiding the user through first-time use.

### Daily Use

1.	The user presses the trigger button; the LCD displays “Listening…” and plays a sound effect.
2.	Recording starts.
3.	The user releases the button; speech recognition begins and converts speech to text.
4.	The recognized text is displayed.
5.	The device displays “Thinking…”, calls the large model, and generates a bitmap.
6.	The bitmap is downloaded, displayed, and printed.
7.	After printing completes, the device enters standby mode.

### Factory Reset

Press and hold the talk button, unplug and reconnect the power cable, and continue holding the button until the LCD screen lights up.

---

## MONO AUDIO CODEC

### Specifications

- 24-bit, 8 to 96 kHz sampling frequency
- 110 dB signal to noise ratio, -80 dB
- Audio clock is 256Fs

### I2C Interface

#### I2C Requirements

- Each bit in a byte is sampled during CCLK high with MSB bit being transmitted firstly. Each transferred byte is followed by an acknowledge bit from receiver to pull the CDATA low. The transfer rate of this interface can be up to 400 kbps.
- ESP32-P4 initiates the transmission by sending a `start` signal, which is defined as a high-to-low transition at CDATA while CCLK is high. The first byte transferred is the slave address. It is a seven-bit chip address followed by a RW bit. The chip address must be `0011 00x`, where `x` equals CE (input pin: 1 being connected to supply and 0 being connected to ground). The RW bit indicates the slave data transfer direction. Once an acknowledge bit is received, the data transfer starts to proceed on a byte-by-byte basis in the direction specified by the RW bit. The master can terminate the communication by generating a “stop” signal, which is defined as a low-to-high transition at CDATA while CCLK is high.

#### I2C GPIO

```c
#define I2C_SCL_IO      GPIO_NUM_8
#define I2C_SDA_IO      GPIO_NUM_7
```

### I2S Interface

### GPIO

```c
#define I2S_MCK_IO      GPIO_NUM_13
#define I2S_BCK_IO      GPIO_NUM_12
#define I2S_WS_IO       GPIO_NUM_10
#define I2S_DO_IO       GPIO_NUM_11
#define I2S_DI_IO       GPIO_NUM_9
```

---

## DISPLAY

### DISPLAY PANEL SPECIFICATIONS

- Display Interface
    - Parallel RGB 16/18/24bits，Max. resolution 1024x768@60fps
- DBU(Display Bridge Unit) input
    - Support 8080 8/16bit parallel protocol
    - Support 6800 8/16bit parallel protocol
    - Support 3/4/4-wire SPI、DSPI、QSPI protocol
    - 8080/6800 8/16bit with max.50MHz clock
    - 3/4/4-wire SPI、4-SDA SPI with max.100MHz clock
- Generic interfaces
    - SPIx2，3WIRE /4/4WIRE
    - UARTRTxTx4，Supports 2-wire/3-wire/4/4-wire interfaces, compatible with industry standard 16550
    - IIC x2，Max. rate 400Kb/s
    - Five GPIO groups with a total of 60 IOs, each IO can be configured independently:
        - The input supports secondary de-bounce and interrupt

### INTERFAFACE DESCRIPTION

If there is no special indication, TR230S works on Mode0, that means data transmissionoccurs on the rising edge of the clock.

IM[1:0] Configuration table(6800 and DSPI is not commonly used and will not be describedin this article):

| IMO | IM1 | Interface     | Related Pins                                                     | Max. Clock |
|-----|-----|---------------|------------------------------------------------------------------|------------|
| 0   | 0   | SPI-4WIRE     | CS#, SCL, SDO0, D/C#, BUSY                                       | 100 MHz    |
| 1   | 0   | QSPI-4SDA     | CS#, SCL, SDO0, SDO1, SDO2, SDO3, BUSY                           | 100 MHz    |
| 0   | 1   | 8080-8bit     | CS#, D0~D7, D/C#, RD#, WR#, BUSY                                 | 50 MHz     |
| 1   | 1   | 8080-16bit    | CS#, D0~D15, D/C#, RD#, WR#, BUSY                                | 50 MHz     |


### SPI INTERFAFACE (SPI-4WIRE, QSPI-4SDA)

#### PIN DESCRIPTION

| PIN NAME | Description |
|----------|-------------|
| CS# | Chip selection: LOW active |
| SCL | Serial Interface Clock |
| SDO0 | SPI: MOSI QSPI: QSPI-DATA0 |
| SDO1 | SPI: MISO QSPI: QSPI-DATA1 |
| SDO2 | QSPI: QSPI-DATA2 |
| SDO3 | QSPI: QSPI-DATA3 |
| D/C# | DATA or COMMAND: flag 0 = COMMAND | flag 1 = DATA |
| BUSY | BUSY status flag: 0 = Sending Command Not Allowed | 1 = Sending Command Allowed |

#### INTERFACE TIMING

- QSPI-4SDA
    - Timing for writing COMMAND and PARAMETERS: (When CODE1=0x02, CODE2=CODE3=0x00)
    - Timing for reading:(When CODE1=0x03, CODE2=CODE3=0x00)

- Display data writing format:(CODE1=0x12, CODE2=CODE3=0x00 COMMAND = 0x2C)

---

### REGISTERS

#### Version (01H)

<table>
  <thead>
    <tr>
      <th colspan="2"></th>
      <th colspan="8">Parameter</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <th></th>
      <th>W/R</th>
      <th>D7</th>
      <th>D6</th>
      <th>D5</th>
      <th>D4</th>
      <th>D3</th>
      <th>D2</th>
      <th>D1</th>
      <th>D0</th>
    </tr>
    <tr>
      <td>1 Parameter</td>
      <td>Read</td>
      <td colspan="4">H</td>
      <td colspan="4">L</td>
    </tr>
    <tr>
      <td>Description</td>
      <td colspan="9">Version: H.L</td>
    </tr>
  </tbody>
</table>

#### Selft Test (12H)

<table>
  <thead>
    <tr>
      <th colspan="2"></th>
      <th colspan="9">Parameter</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <th></th>
      <th>W/R</th>
      <th>D7</th>
      <th>D6</th>
      <th>D5</th>
      <th>D4</th>
      <th>D3</th>
      <th>D2</th>
      <th>D1</th>
      <th>D0</th>
      <th>Def</th>
    </tr>
    <tr>
      <td>1 Parameter</td>
      <td>Write</td>
      <td colspan="8">Format</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>Description</td>
      <td colspan="10">0x00: Self test OFF; 0x01: Self test ON</td>
    </tr>
  </tbody>
</table>

#### PWM Duty Ratio Setting (20H)

<table>
  <thead>
    <tr>
      <th colspan="2"></th>
      <th colspan="9">Parameter</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <th></th>
      <th>W/R</th>
      <th>D7</th>
      <th>D6</th>
      <th>D5</th>
      <th>D4</th>
      <th>D3</th>
      <th>D2</th>
      <th>D1</th>
      <th>D0</th>
      <th>Def</th>
    </tr>
    <tr>
      <td>1 Parameter</td>
      <td>Write</td>
      <td colspan="8">Duty</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>Description</td>
      <td colspan="10">PWM duty ratio setting for backlight control: 0~100 (0x64 means 100% duty)</td>
    </tr>
  </tbody>
</table>

#### PWM Frequency Setting (21H)

<table>
  <thead>
    <tr>
      <th colspan="2"></th>
      <th colspan="9">Parameter</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <th></th>
      <th>W/R</th>
      <th>D7</th>
      <th>D6</th>
      <th>D5</th>
      <th>D4</th>
      <th>D3</th>
      <th>D2</th>
      <th>D1</th>
      <th>D0</th>
      <th>Def</th>
    </tr>
    <tr>
      <td>1 Parameter</td>
      <td>Write</td>
      <td colspan="8">CLK</td>
      <td>0AH</td>
    </tr>
    <tr>
      <td>Description</td>
      <td colspan="10">PPWM frequency setting for backlight control : 0~100KHz (0x64 means 100KHz)</td>
    </tr>
  </tbody>
</table>

#### Display Off (28H)

<table>
  <thead>
    <tr>
      <th colspan="2"></th>
      <th colspan="8">Parameter</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <th></th>
      <th>W/R</th>
      <th>D7</th>
      <th>D6</th>
      <th>D5</th>
      <th>D4</th>
      <th>D3</th>
      <th>D2</th>
      <th>D1</th>
      <th>D0</th>
    </tr>
    <tr>
      <td>1 Parameter</td>
      <td>Write</td>
      <td colspan="8">No parameter</td>
    </tr>
    <tr>
      <td>Description</td>
      <td colspan="9">Display on</td>
    </tr>
  </tbody>
</table>

#### Display On (29H)

<table>
  <thead>
    <tr>
      <th colspan="2"></th>
      <th colspan="8">Parameter</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <th></th>
      <th>W/R</th>
      <th>D7</th>
      <th>D6</th>
      <th>D5</th>
      <th>D4</th>
      <th>D3</th>
      <th>D2</th>
      <th>D1</th>
      <th>D0</th>
    </tr>
    <tr>
      <td>1 Parameter</td>
      <td>Write</td>
      <td colspan="8">No parameter</td>
    </tr>
    <tr>
      <td>Description</td>
      <td colspan="9">Display on</td>
    </tr>
  </tbody>
</table>

#### COL_ADR (2AH)

<table>
  <thead>
    <tr>
      <th colspan="2"></th>
      <th colspan="9">Parameter</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <th></th>
      <th>W/R</th>
      <th>D7</th>
      <th>D6</th>
      <th>D5</th>
      <th>D4</th>
      <th>D3</th>
      <th>D2</th>
      <th>D1</th>
      <th>D0</th>
      <th>Def</th>
    </tr>
    <tr>
      <td>1 Parameter</td>
      <td>Write</td>
      <td colspan="8">X_S(H)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>2 Parameter</td>
      <td>Write</td>
      <td colspan="8">X_S(L)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>3 Parameter</td>
      <td>Write</td>
      <td colspan="8">X_E(H)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>4 Parameter</td>
      <td>Write</td>
      <td colspan="8">X_E(L)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>Description</td>
      <td colspan="10">Indicates the display size</td>
    </tr>
  </tbody>
</table>

#### ROW_ADDR (2BH)

<table>
  <thead>
    <tr>
      <th colspan="2"></th>
      <th colspan="9">Parameter</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <th></th>
      <th>W/R</th>
      <th>D7</th>
      <th>D6</th>
      <th>D5</th>
      <th>D4</th>
      <th>D3</th>
      <th>D2</th>
      <th>D1</th>
      <th>D0</th>
      <th>Def</th>
    </tr>
    <tr>
      <td>1 Parameter</td>
      <td>Write</td>
      <td colspan="8">Y_S(H)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>2 Parameter</td>
      <td>Write</td>
      <td colspan="8">Y_S(L)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>3 Parameter</td>
      <td>Write</td>
      <td colspan="8">Y_E(H)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>4 Parameter</td>
      <td>Write</td>
      <td colspan="8">Y_E(L)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>Description</td>
      <td colspan="10">Indicates the display size</td>
    </tr>
  </tbody>
</table>

---

#### Display Data (2CH)

<table>
  <thead>
    <tr>
      <th colspan="2"></th>
      <th colspan="9">Parameter</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <th></th>
      <th>W/R</th>
      <th>D7</th>
      <th>D6</th>
      <th>D5</th>
      <th>D4</th>
      <th>D3</th>
      <th>D2</th>
      <th>D1</th>
      <th>D0</th>
      <th>Def</th>
    </tr>
    <tr>
      <td>1 Parameter</td>
      <td>Write</td>
      <td colspan="8">Display Data</td>
      <td></td>
    </tr>
    <tr>
      <td>Description</td>
      <td colspan="10">Display data push command</td>
    </tr>
  </tbody>
</table>

---

#### Display Format (3AH)

<table>
  <thead>
    <tr>
      <th colspan="2"></th>
      <th colspan="9">Parameter</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <th></th>
      <th>W/R</th>
      <th>D7</th>
      <th>D6</th>
      <th>D5</th>
      <th>D4</th>
      <th>D3</th>
      <th>D2</th>
      <th>D1</th>
      <th>D0</th>
      <th>Def</th>
    </tr>
    <tr>
      <td>1 Parameter</td>
      <td>Write</td>
      <td colspan="8">Format</td>
      <td>55H</td>
    </tr>
    <tr>
      <td>Description</td>
      <td colspan="10">RGB format for input signal：0x55 = RGB565 | 0x66 = RGB666 | 0x77 = RGB888</td>
    </tr>
  </tbody>
</table>

---

#### Software Reset (5AH)

<table>
  <thead>
    <tr>
      <th colspan="2"></th>
      <th colspan="8">Parameter</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <th></th>
      <th>W/R</th>
      <th>D7</th>
      <th>D6</th>
      <th>D5</th>
      <th>D4</th>
      <th>D3</th>
      <th>D2</th>
      <th>D1</th>
      <th>D0</th>
    </tr>
    <tr>
      <td>1 Parameter</td>
      <td>Write</td>
      <td colspan="8">0x01</td>
    </tr>
    <tr>
      <td>Description</td>
      <td colspan="10">Soft reset</td>
    </tr>
  </tbody>
</table>

---

#### RGB Interface Setting (70H)

<table>
  <thead>
    <tr>
      <th colspan="2"></th>
      <th colspan="9">Parameter</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <th></th>
      <th>W/R</th>
      <th>D7</th>
      <th>D6</th>
      <th>D5</th>
      <th>D4</th>
      <th>D3</th>
      <th>D2</th>
      <th>D1</th>
      <th>D0</th>
      <th>Def</th>
    </tr>
    <tr>
      <td>1 Parameter</td>
      <td>Write</td>
      <td colspan="8">Mode</td>
      <td>01H</td>
    </tr>
    <tr>
      <td>2 Parameter</td>
      <td>Write</td>
      <td colspan="8">Format</td>
      <td>01H</td>
    </tr>
    <tr>
      <td>3 Parameter</td>
      <td>Write</td>
      <td colspan="8">Clock Phase</td>
      <td>01H</td>
    </tr>
    <tr>
      <td>4 Parameter</td>
      <td>Write</td>
      <td colspan="8">Data Order</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>5 Parameter</td>
      <td>Write</td>
      <td colspan="8">Data Mirror</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>Description</td>
      <td colspan="10">
        1. Mode: 0x01 = Parallel RGB<br />
        2. Format: 0x01 = RGB24bits(Mapping 0); 0x02 = RGB18 with Low bits drop(Mapping 1); 0x03 = RGB18 with high bits drop (Mapping 2); 0x04 = RGB16 with Low bits drop (Mapping 3); 0x05 = RGB16 with high bits drop (Mapping 4)<br />
        3. Clock Phase: 0x01 = 0°; 0x02 = 90°; 0x03 = 180°; 0x04 = 270°<br />
        4. Data Order: 0x01 = RGB; 0x02 = RBG; 0x03 = BGR; 0x04 = BRG; 0x05 = GRB; 0x06 = GBR<br />
        5. Data Mirror: 0x00 = Output RGB data from LSB to MSB; 0x01 = Output RGB data from MSB to LSB</td>
    </tr>
  </tbody>
</table>

---

#### Resolution and Clock Setting (71H)

<table>
  <thead>
    <tr>
      <th colspan="2"></th>
      <th colspan="9">Parameter</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <th></th>
      <th>W/R</th>
      <th>D7</th>
      <th>D6</th>
      <th>D5</th>
      <th>D4</th>
      <th>D3</th>
      <th>D2</th>
      <th>D1</th>
      <th>D0</th>
      <th>Def</th>
    </tr>
    <tr>
      <td>1 Parameter</td>
      <td>Write</td>
      <td colspan="8">CLK</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>2 Parameter</td>
      <td>Write</td>
      <td colspan="8">Hactive(H)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>3 Parameter</td>
      <td>Write</td>
      <td colspan="8">Hactive(L)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>4 Parameter</td>
      <td>Write</td>
      <td colspan="8">Vactive(H)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>5 Parameter</td>
      <td>Write</td>
      <td colspan="8">Vactive(L)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>Description</td>
      <td colspan="10">CLK: Pixel clock setting, range with 0~48MHz; (0x30 means 48MHz)<br />Hactive/Vactive: Resolution setting</td>
    </tr>
  </tbody>
</table>

---

#### Horizontal Porch Setting (72H)

<table>
  <thead>
    <tr>
      <th colspan="2"></th>
      <th colspan="9">Parameter</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <th></th>
      <th>W/R</th>
      <th>D7</th>
      <th>D6</th>
      <th>D5</th>
      <th>D4</th>
      <th>D3</th>
      <th>D2</th>
      <th>D1</th>
      <th>D0</th>
      <th>Def</th>
    </tr>
    <tr>
      <td>1 Parameter</td>
      <td>Write</td>
      <td colspan="8">H-Front Porch (H)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>2 Parameter</td>
      <td>Write</td>
      <td colspan="8">H-Front Porch (L)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>3 Parameter</td>
      <td>Write</td>
      <td colspan="8">H-Back Porch (H)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>4 Parameter</td>
      <td>Write</td>
      <td colspan="8">H-Back Porch (L)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>5 Parameter</td>
      <td>Write</td>
      <td colspan="8">H-Sync Pulse Width (H)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>6 Parameter</td>
      <td>Write</td>
      <td colspan="8">H-Sync Pulse Width (L)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>Description</td>
      <td colspan="10">Horizontal porch setting</td>
    </tr>
  </tbody>
</table>

---

#### Vertical Porch Setting (73H)

<table>
  <thead>
    <tr>
      <th colspan="2"></th>
      <th colspan="9">Parameter</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <th></th>
      <th>W/R</th>
      <th>D7</th>
      <th>D6</th>
      <th>D5</th>
      <th>D4</th>
      <th>D3</th>
      <th>D2</th>
      <th>D1</th>
      <th>D0</th>
      <th>Def</th>
    </tr>
    <tr>
      <td>1 Parameter</td>
      <td>Write</td>
      <td colspan="8">V-Front Porch (H)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>2 Parameter</td>
      <td>Write</td>
      <td colspan="8">V-Front Porch (L)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>3 Parameter</td>
      <td>Write</td>
      <td colspan="8">V-Back Porch (H)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>4 Parameter</td>
      <td>Write</td>
      <td colspan="8">V-Back Porch (L)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>5 Parameter</td>
      <td>Write</td>
      <td colspan="8">V-Sync Pulse Width (H)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>6 Parameter</td>
      <td>Write</td>
      <td colspan="8">V-Sync Pulse Width (L)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>Description</td>
      <td colspan="10">Vertical porch setting</td>
    </tr>
  </tbody>
</table>

---

#### FONT Property Setting (75H)

<table>
  <thead>
    <tr>
      <th colspan="2"></th>
      <th colspan="9">Parameter</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <th></th>
      <th>W/R</th>
      <th>D7</th>
      <th>D6</th>
      <th>D5</th>
      <th>D4</th>
      <th>D3</th>
      <th>D2</th>
      <th>D1</th>
      <th>D0</th>
      <th>Def</th>
    </tr>
    <tr>
      <td>1 Parameter</td>
      <td>Write</td>
      <td colspan="8">Kerning(H)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>2 Parameter</td>
      <td>Write</td>
      <td colspan="8">Kerning(L)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>3 Parameter</td>
      <td>Write</td>
      <td colspan="8">Space(H)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>4 Parameter</td>
      <td>Write</td>
      <td colspan="8">Space(L)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>5 Parameter</td>
      <td>Write</td>
      <td colspan="4">F_E</td>
      <td colspan="4">B_E</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>6 Parameter</td>
      <td>Write</td>
      <td colspan="8">F_COLOR(H)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>7 Parameter</td>
      <td>Write</td>
      <td colspan="8">F_COLOR(L)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>8 Parameter</td>
      <td>Write</td>
      <td colspan="8">B_COLOR(H)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>9 Parameter</td>
      <td>Write</td>
      <td colspan="8">B_COLOR(L)</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>Description</td>
      <td colspan="10">Kerning: Font spaceing<br />Space: Line spacing<br />F_E: 0x0 = Enable foreground color / 0x1: Disable foreground color<br />B_E: 0x0 = Enable background color / 0x1 = Disable background color<br /> F_COLOR: Foreground color<br />B_COLOR: Background color</td>
    </tr>
  </tbody>
</table>

---

#### Drawing LINE (B0H)

<table>
  <thead>
    <tr>
      <th colspan="2"></th>
      <th colspan="8">Parameter</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <th></th>
      <th>W/R</th>
      <th>D7</th>
      <th>D6</th>
      <th>D5</th>
      <th>D4</th>
      <th>D3</th>
      <th>D2</th>
      <th>D1</th>
      <th>D0</th>
    </tr>
    <tr>
      <td>1 Parameter</td>
      <td>Write</td>
      <td colspan="8">X_S (H)</td>
    </tr>
    <tr>
      <td>2 Parameter</td>
      <td>Write</td>
      <td colspan="8">X_S (L)</td>
    </tr>
    <tr>
      <td>3 Parameter</td>
      <td>Write</td>
      <td colspan="8">Y_S (H)</td>
    </tr>
    <tr>
      <td>4 Parameter</td>
      <td>Write</td>
      <td colspan="8">Y_S (L)</td>
    </tr>
    <tr>
      <td>5 Parameter</td>
      <td>Write</td>
      <td colspan="8">X_E (H)</td>
    </tr>
    <tr>
      <td>6 Parameter</td>
      <td>Write</td>
      <td colspan="8">X_E (L)</td>
    </tr>
    <tr>
      <td>7 Parameter</td>
      <td>Write</td>
      <td colspan="8">Y_E (H)</td>
    </tr>
    <tr>
      <td>8 Parameter</td>
      <td>Write</td>
      <td colspan="8">Y_E (L)</td>
    </tr>
    <tr>
      <td>9 Parameter</td>
      <td>Write</td>
      <td colspan="8">Width</td>
    </tr>
    <tr>
      <td>10 Parameter</td>
      <td>Write</td>
      <td colspan="8">COLOR (H)</td>
    </tr>
    <tr>
      <td>11 Parameter</td>
      <td>Write</td>
      <td colspan="8">COLOR (L)</td>
    </tr>
    <tr>
      <td>Description</td>
      <td colspan="9">Start (X_S, Y_S) <br /> End (X_E, Y_E) <br /> Width: Line width <br /> Color: Line color</td>
    </tr>
  </tbody>
</table>

---

#### Drawing Circle (B1H)

<table>
  <thead>
    <tr>
      <th colspan="2"></th>
      <th colspan="8">Parameter</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <th></th>
      <th>W/R</th>
      <th>D7</th>
      <th>D6</th>
      <th>D5</th>
      <th>D4</th>
      <th>D3</th>
      <th>D2</th>
      <th>D1</th>
      <th>D0</th>
    </tr>
    <tr>
      <td>1 Parameter</td>
      <td>Write</td>
      <td colspan="8">X_S (H)</td>
    </tr>
    <tr>
      <td>2 Parameter</td>
      <td>Write</td>
      <td colspan="8">X_S (L)</td>
    </tr>
    <tr>
      <td>3 Parameter</td>
      <td>Write</td>
      <td colspan="8">Y_S (H)</td>
    </tr>
    <tr>
      <td>4 Parameter</td>
      <td>Write</td>
      <td colspan="8">Y_S (L)</td>
    </tr>
    <tr>
      <td>5 Parameter</td>
      <td>Write</td>
      <td colspan="8">X_E (H)</td>
    </tr>
    <tr>
      <td>6 Parameter</td>
      <td>Write</td>
      <td colspan="8">X_E (L)</td>
    </tr>
    <tr>
      <td>7 Parameter</td>
      <td>Write</td>
      <td colspan="8">Y_E (H)</td>
    </tr>
    <tr>
      <td>8 Parameter</td>
      <td>Write</td>
      <td colspan="8">Y_E (L)</td>
    </tr>
    <tr>
      <td>9 Parameter</td>
      <td>Write</td>
      <td colspan="8">Width</td>
    </tr>
    <tr>
      <td>10 Parameter</td>
      <td>Write</td>
      <td colspan="8">COLOR (H)</td>
    </tr>
    <tr>
      <td>11 Parameter</td>
      <td>Write</td>
      <td colspan="8">COLOR (L)</td>
    </tr>
    <tr>
      <td>Description</td>
      <td colspan="9">Start (X_S, Y_S) <br /> End (X_E, Y_E) <br /> Width: Line width <br /> Color: Line color</td>
    </tr>
  </tbody>
</table>

---

#### Drawing Rectangle (B2H)

<table>
  <thead>
    <tr>
      <th colspan="2"></th>
      <th colspan="8">Parameter</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <th></th>
      <th>W/R</th>
      <th>D7</th>
      <th>D6</th>
      <th>D5</th>
      <th>D4</th>
      <th>D3</th>
      <th>D2</th>
      <th>D1</th>
      <th>D0</th>
    </tr>
    <tr>
      <td>1 Parameter</td>
      <td>Write</td>
      <td colspan="8">X1 (H)</td>
    </tr>
    <tr>
      <td>2 Parameter</td>
      <td>Write</td>
      <td colspan="8">X1 (L)</td>
    </tr>
    <tr>
      <td>3 Parameter</td>
      <td>Write</td>
      <td colspan="8">Y1 (H)</td>
    </tr>
    <tr>
      <td>4 Parameter</td>
      <td>Write</td>
      <td colspan="8">Y1 (L)</td>
    </tr>
    <tr>
      <td>5 Parameter</td>
      <td>Write</td>
      <td colspan="8">X2 (H)</td>
    </tr>
    <tr>
      <td>6 Parameter</td>
      <td>Write</td>
      <td colspan="8">X2 (L)</td>
    </tr>
    <tr>
      <td>7 Parameter</td>
      <td>Write</td>
      <td colspan="8">Y2 (H)</td>
    </tr>
    <tr>
      <td>8 Parameter</td>
      <td>Write</td>
      <td colspan="8">Y2 (L)</td>
    </tr>
    <tr>
      <td>9 Parameter</td>
      <td>Write</td>
      <td colspan="8">COLOR (H)</td>
    </tr>
    <tr>
      <td>10 Parameter</td>
      <td>Write</td>
      <td colspan="8">COLOR (L)</td>
    </tr>
    <tr>
      <td>Description</td>
      <td colspan="9">Coordinate of the upper left corner of the rectangle (X1, Y1)<br />Coordinate of the bottom right corner of the rectangle (X2, Y2)</td>
    </tr>
  </tbody>
</table>

---

#### Mirror and Rotation (ACH)

<table>
  <thead>
    <tr>
      <th colspan="2"></th>
      <th colspan="9">Parameter</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <th></th>
      <th>W/R</th>
      <th>D7</th>
      <th>D6</th>
      <th>D5</th>
      <th>D4</th>
      <th>D3</th>
      <th>D2</th>
      <th>D1</th>
      <th>D0</th>
      <th>Def</th>
    </tr>
    <tr>
      <td>1 Parameter</td>
      <td>Write</td>
      <td colspan="8">Format</td>
      <td>00H</td>
    </tr>
    <tr>
      <td>Description</td>
      <td colspan="10">
        0x00 = No Rotation<br />
        0x01 = Rotation 90°<br />
        0x02 = Rotation 180°<br />
        0x03 = Rotation 270°<br />
        0x10 = X-axis mirror image<br />
        0x20 = Y-axis mirror image<br />
        0x40 = Scaling
      </td>
    </tr>
  </tbody>
</table>

---

## THERMAL PRINTER

### Thermal Printer Example Code

```c
#include "driver/gpio.h"
#include "driver/uart.h"
#define PRINTER_UART_PORT UART_NUM_1
#define PRINTER_UART_TXD GPIO_NUM_20
#define PRINTER_UART_RXD GPIO_NUM_21
#define PRINTER_UART_DTR GPIO_NUM_22
#define PRINTER_UART_BAUD_RATE 9600

static void printer_uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = PRINTER_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(PRINTER_UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(PRINTER_UART_PORT, PRINTER_UART_TXD, PRINTER_UART_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(PRINTER_UART_PORT, 1024, 0, 0, NULL, 0));
}

static void printer_dtr_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << PRINTER_UART_DTR,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(PRINTER_UART_DTR, 1);
}

static void printer_self_test(void)
{
    const uint8_t init_cmd[] = {0x1B, 0x40};
    const uint8_t self_test_cmd[] = {0x12, 0x54};

    ESP_LOGI(PRINTER_TAG, "sending printer init");
    uart_write_bytes(PRINTER_UART_PORT, (const char *)init_cmd, sizeof(init_cmd));
    vTaskDelay(pdMS_TO_TICKS(20));

    ESP_LOGI(PRINTER_TAG, "sending printer self-test");
    uart_write_bytes(PRINTER_UART_PORT, (const char *)self_test_cmd, sizeof(self_test_cmd));
}
```

```c
printer_uart_init();
printer_dtr_init();
printer_self_test();
```

---

### Thermal Printer TTL Protocol

## 指令集

### 初始化

Command(HEX): 1B 40
Description: Clean printer cache, reset all settings to defaults

---

### 打印自测试页

Command(HEX): 12 54
Description: 打印机打印一张自测页，上面包含打印机的程序版本，通讯接口类型，代码页和其他一些数据

---

### 设置字符打印方式

Command(HEX): 1B 21 n
Description: 设置字符打印方式（字型、反白、倒置、粗体、倍高、倍宽、和下划线），

参数 n 定义如下：

| 位 | 功能   | 值         |
|----|--------|------------|
| 0  | 字型   | 正常 / 小字 |
| 1  | 未定义 | -          |
| 2  | 未定义 | -          |
| 3  | 粗体   | 取消 / 设定 |
| 4  | 倍高   | 取消 / 设定 |
| 5  | 倍宽   | 取消 / 设定 |
| 6  | 未定义 | -          |
| 7  | 下划线 | 取消 / 设定 |

Example:

1B 40 1B 21 01 30 31 32 0D 0A
1B 40 1B 21 02 30 31 32 0D 0A
1B 40 1B 21 04 30 31 32 0D 0A
1B 40 1B 21 08 30 31 32 0D 0A

---

### 设定字符大

Command(HEX): 1d 21 n
Description: 设定字符大小

参数 n 定义如下：

00 正常字体(默认)
11 倍高倍宽
10 字体倍宽
01 字体倍高

Example:

1B 40 1d 21 00
CF C3 C3 C5 B4 EF C6 D5 B5 E7 D7 D3 0d 0A
1B 40 1d 21 11
CF C3 C3 C5 B4 EF C6 D5 B5 E7 D7 D3 0d 0A
1B 40 1d 21 10
CF C3 C3 C5 B4 EF C6 D5 B5 E7 D7 D3 0d 0A
1B 40 1d 21 01
CF C3 C3 C5 B4 EF C6 D5 B5 E7 D7 D3 0d 0A

---

### 设置打印对齐方式

Command(HEX): 1B 61 n
Description: 对一行中的所有数据进行对齐处理, 设置打印对齐方式（居左、居中、居右）

参数 n 定义如下：
0, 48 居左
1, 49 居中
2, 50 居右

默认值：0

Example:

1B 40 1B 61 00
C4 AC C8 CF D7 F3 B6 D4 C6 EB 0D 0A
1B 40 1B 61 01
BE D3 D6 D0 B6 D4 C6 EB 0D 0A
1B 40 1B 61 02
BF BF D3 D2 B6 D4 C6 EB 0D 0A

---

### 水平位置打印行线段 (曲线打印命令)

Command(HEX): 29 39 n x1sL x1eH x1eL x1eH ...xnsL xnsH xneL xneH
Description: 打印放大图如下所示：每个水平曲线段可以视为由段长度为 1 的这些点组成。打印 n 行水平线段的，连续使用该命令就可以打印出所需的曲线。

Example:

xksL : K 线起点低阶的水平坐标；
xksH : K 线起点高阶的水平坐标；
xkeL : K 线结束点低阶的水平坐标；
xkeH : K 线结束点高阶的水平坐标；
坐标开始位置通常是打印区域的左边。最小坐标坐标为（0,0），最大横坐标值 383，xkeL+xkeH*256, 行数据可以不按规定范围内顺序排列；

Example Code:

```c
Char SendStr[8];
Char SendStr2[16];
Float i;
Short y1,y2,y1s,y2s;
//打印 Y 轴（一条线）
SendStr[0]=0x1D;
SendStr[1]=0x27;
SendStr[2]=1； //一行
SendStr[3]=30
SendStr[4]=0; //开始点
SendStr[5]=104;
SendStr[6]=1; //结束点
PreSendData(SendStr,7);
//Print curve
SendStr[0]=0x1D;
SendStr[1]=0x27;
SendStr[2]=3; X 轴，sin 和 cos
//Three lines:X-axis,sin and cos function curve 三条线：
函数
SendStr[3]=180; SendStr[4]=0; // X 轴位置
SendStr[5]=180; SendStr[6]=0;
for(i=1;i<1200;i++)
{
  y1=sin(i/180*3.1416)*(380-30)/2+180; //计算 sin 函数坐标
  y2=cos(i/180*3.1416)*(380-30)/2+180; //计算 cos 函数坐标
  If(i==1){y1s=y1;y2s=y2;}
  PreSendData(SendStr,7);
  If(y1s<y1)
  {
    PreSendData(&y1s,2); //sin 函数在该行的起始点
    PreSendData(&y1,2); //sin 函数在该行的结束点
  }
  Else
  {
    PreSendData(&y1,2); //sin 函数在该行的起始点
    PreSendData(&y1s,2); //sin 函数在该行的结束点
  }
  
  If(y2s<y2) {
    PreSendData(&y2s,2); //cos 函数在该行的起始点
    PreSendData(&y2,2); //cos 函数在该行的结束点
  }
  Else
  {
    PreSendData(&y2,2); //cos 函数在该行的起始点
    PreSendData(&y2s,2); //cos 函数在该行的结束点
    y1s=y1; // 当打印进入下一行，sin 函数曲线起点横坐标
    y2s=y2; //当打印进入下一行，cos 函数曲线起点横坐标
  }
}
```

---

### 设置行间距为 n 点

Command(HEX): 1B 33 n
Description: 设置行间距为 n 点
Value n range: 0 <= n <= 255

Example:

1B 40
1B 33 30
CF C3 C3 C5 B4 EF C6 D5 B5 E7 D7 D3 BF C6 BC BC D3 D0 CF DE B9 AB CB
BE 0D 0A
CF C3 C3 C5 B4 EF C6 D5 B5 E7 D7 D3 BF C6 BC BC D3 D0 CF DE B9 AB CB
BE 0D 0A
1B 33 50
CF C3 C3 C5 B4 EF C6 D5 B5 E7 D7 D3 BF C6 BC BC D3 D0 CF DE B9 AB CB
BE 0D 0A
CF C3 C3 C5 B4 EF C6 D5 B5 E7 D7 D3 BF C6 BC BC D3 D0 CF DE B9 AB CB
BE 0D 0A

---

### 读取缺纸状态

Command(HEX): 10 04 01
Description: 检查缺纸状态, 当打印机缺纸时会自动返回缺纸状态 `EF 23 1A` 一秒返回一次，直到装纸成功会返回有纸状态 `FE 23 12` 只返回一次

Example:

发送检查缺纸指令：`10 04 01` ，发送一次就返回一次数据
返回数据:
FE 23 12 (打印机有纸)
EF 23 1A (打印机缺纸)

---

### 打印状态

发送打印机数据打印机打印完成隔 500ms（毫秒）没有数据发送打印机就自动返回数据：FC 4F 4B（打印完成）
发送数据打印过程中出现缺纸的情况会返回 FC 6E 6F（打印失败）

---

### Set the printer serial port baud rate

Command(HEX): 1F 2D 55 01 m
Description: Set the printer serial port baud rate
Default m Value: 5

| m | baudrate  | m | baudrate  |
|------|---------|------|----------|
| 0    | 1200    | 15   | 307200   |
| 1    | 2400    | 16   | 460800   |
| 2    | 3600    | 17   | 614400   |
| 3    | 4800    | 18   | 921600   |
| 4    | 7200    | 19   | 1228800  |
| 5    | 9600    | 20   | 1843200  |
| 6    | 14400   |      |          |
| 7    | 19200   |      |          |
| 8    | 28800   |      |          |
| 9    | 38400   |      |          |
| 10   | 57600   |      |          |
| 11   | 76800   |      |          |
| 12   | 115200  |      |          |
| 13   | 153600  |      |          |
| 14   | 230400  |      |          |

---

## 设置断电默认串口打开或关闭状态

Command(HEX): 1F 2D 71 01 m
Description: 设置串口通信打开与关闭

M=0, 打开串口 M= 1，关闭串口

防止设备与设备串口通信过程中，如果：设置串口关闭模式，打印机接收任何数据都不会处理，防止在不打印情况下出现数据干扰，影响到打印机正常工作

Default m Value: 0

重新上电有效。这个指令只是做前期设置使用，打印过程中不需要调用该指令

Example:
下发指令：1F 2D 71 01 m

下发设置数据后打印机会打印出“Successfully Set The Uart open state is Close”

返回数据：1F 2D 71 01 m

---

## 设置串口状态（该指令断电不保存）

Command(HEX): 1F 77 m
Description: 该指令是在打印过程中使用，发送数据前先设置串口为打开模式，再发送打印数据，然后在发送关闭串口

- M=0: 打开串口
- M=1: 关闭串口

先打开串口----发送打印数据---再关闭串口

Example:

```text
1F 77 00 (先打开串口)
1b 40
1b 33 30
CF C3 C3 C5 B4 EF C6 D5 B5 E7 D7 D3 BF C6 BC BC D3 D0 CF DE B9 AB CB
BE 0d 0a
1F 77 01 (再关闭串口)
```

---

## 设置是否进纸、进纸行数、结束数据多长时间

Command(HEX): 1F 2D 35 04 m k tL tH

Description: 数据打印完成后设置是否进纸、进纸行数、结束数据多长时间开始进纸

- 10<=tL + tH *256 <=1000;n=0,1;1<=k<=256;
- m=0: 进纸
- m=1: 不进纸
- k: 进纸行数
- tL+tH*256: 判断数据结束的时间，默认 200ms

Example:

```text
Command: 1F 2D 35 04 00 05 C8 00
Return: 1F 2D 35 04 00 05 C8 00
```

---

## Full Examples

### Barcode Printing

```text
1B 40 1B 61 01 1D 48 02 1D 68 50 00 1D 77 02 00 1D 6B 49 0b 31 32 33 34 35 36 37 38 39 31 30
1B 40 初始化打印机
1B 61 00//条码居靠左
1B 61 01//条码居中
1B 61 02//条码居靠左
1D 48 01 //数据在条码上方显示
1D 48 02 //数据在条码下方显示
1D 48 03 //数据在条码上下显示
33
1D 48 00 //不显示数字只有条码
1D 68 50 00 //1D 68 设置条码高度 50 00 为 80 高度， 高度范围在 10-200
1D 77 02 00 //1D 77 设置条码宽度 03 00 为 2 宽度，宽度范围在 16
1D 6B 49 //条码类型 CODE128
0B 31 32 33 34 35 36 37 38 39 31 30 // 0B为数据长度11 ，条码数据31 32 33 34 35 36 37 38 39 31 30内容“12345678910”
注意条码不支持有中文字符和汉字
```

### Text Printing

```text
1B 40 1B 33 10 1D 21 11 1B 61 01 BB B6 D3 AD B9 E2 C1 D9 0D 0A
1B 40 1B 33 10 //设定行高距离 10 行距范围 10,20,30,40,50,60
1B 40 1B 33 20
1B 40 1B 33 30
1B 40 1B 33 40
1B 40 1B 33 50
1B 40 1B 33 60
1D 21 00 //正常字体大小
1D 21 11 //字体放大一倍
1D 21 10 //字体宽度放大一倍
1D 21 01 //字体高度放大一倍
1B 61 00 //文本左对齐
1B 61 01 //文本居中对齐
1B 61 02 //文本右对齐
BB B6 D3 AD B9 E2 C1 D9 文本打印内容“欢迎光临”
0D 0A 结束符，也可当换行使用
```

### QR Code Printing

```text
1B 40 //固定
1D 28 6B 03 00 31 `43 03` //二维码大小 43 02、43 03、43 04、43 05、43 06、43 07、43 08
1d 28 6B 03 00 31 45 30 //固定
1d 28 6B 06 00 31 50 30 41 42 43 //06 00 数据长度(31 50 30 41 42 43)6 个数据长度，
31 50 30 固定，41 42 43 二维码内容“ABC”
1b 61 01// 00 二维码居左 01 二维码居中 10 二维码居右
1d 28 6B 03 00 31 52 30//固定
1d 28 6B 03 00 31 51 30//固定

文本内容打印
1b 40//固定
1b 61 01//00 居左 01 居中 10 居右
1d 21 00//00 正常 01 倍宽 10 倍高 11 倍宽高
C9 A8 D2 BB C9 A8 B9 D8 D7 A2//文本内容"扫一扫关注" 0d 0a //换行
1b 40
1d 28 6b 03 00 31 43 08
1d 28 6b 03 00 31 45 30
1d 28 6b 06 00 31 50 30 41 42 43
1b 61 01
1d 28 6b 03 00 31 52 30
1d 28 6b 03 00 31 51 30
1b 40 1d 21 00
1b 61 01
C9 A8 D2 BB C9 A8 B9 D8 D7 A2 0d 0a 0d 0a 0d 0a 0d 0a 0d 0a 1b 69
```

---

## 图形打印指令

### 图形垂直取模数据填充

Command (HEX): 1B 2A m Hl Hh [d]k

Description: 打印纵向取模图像数据，参数意义如下：
- `m` 为点图格式：

| m  | 模式           | 水平比例 | 垂直比例 |
|----|----------------|----------|----------|
| 0  | 8 点单密度     | ×2       | ×3       |
| 1  | 8 点双密度     | ×1       | ×3       |
| 32 | 24 点单密度    | ×2       | ×1       |
| 33 | 24 点双密度    | ×1       | ×1       |

- `Hl` `Hh` 为水平方向点数（Hl＋256×Hh）
- [d]k 为点图数据
- `k` 用于指示点图数据字节数，不参加传输

参数范围：

XX58：
  m = 0、1、32、33
  1 ≤ Hl + Hh × 256 ≤ 384
  0 ≤ d ≤ 255
  k = Hl + Hh × 256（当 m = 0、1）
  k = ( Hl + Hh × 256 ) × 3（当 m = 32、33）
  XX80：
  m = 0、1、32、33
  1 ≤ Hl + Hh × 256 ≤ 576
  0 ≤ d ≤ 255
  k = Hl + Hh × 256（当 m = 0、1）
  k = ( Hl + Hh × 256 ) × 3（当 m = 32、33）

---

### 横向图片图片打印

```text
1D 76 30 00 07 00 2F 00
35
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 07 F0 00 00 00 00 01 FF FF 58 00 00 00 1F DF FF FC
00 00 00 E0 00 40 FC 00 00 07 80 00 01 FC 00 06 1E 00 7E 07 FE 00 01 7E 03 FE 1F FE 80 1C 44 07 FE 3F FE C0 1F 80 07 E0 3F F8
C0 1E C0 00 00 FF E1 80 1E 40 00 33 FF 07 00 1E 60 00 00 7F 00 00 7F 34 00 00 FE 00 00 7F 72 00 07 FC 00 00 7F CC 00 F1 FC
00 00 7E 87 00 01 F8 00 00 3C 03 C1 C3 F0 00 00 00 01 FF E7 E0 00 00 00 00 7F F7 C0 00 00 00 00 1F F7 80 00 00 00 00 07 FF
00 00 00 00 00 01 FE 00 00 00 00 00 00 7E 00 00 00 00 00 00 7E 00 00 00 00 00 00 7E 00 00 00 00 00 00 6E 00 00 00 00 00 00
F6 00 00 00 00 00 00 C6 00 00 00 00 00 01 C2 00 00 00 00 00 01 C1 00 00 00 00 00 01 81 00 00 00 00 00 03 81 00 00 00 00 00
07 01 00 00 00 00 00 07 00 00 00 00 00 00 07 00 00 00 00 00 00 07 00 00 00 00 00 00 06 40 00 00 00 00 00 09 40 00 00 00 00
00 09 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
1b6d
1D 76 30 //打印横向取模图像数据
00 //第四位数据 正常图片大小
07 00 图片宽 图片实际宽度除以 8，得到是字节(bit)数据
2F 00 图片高
后面是图片数据
```