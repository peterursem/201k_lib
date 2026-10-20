# 201k_lib

A C library for ESP32-S3 to drive the DisplayTech LightView LDM-201k microprojector module. This code is based off a masters thesis work written by Ladislav Krivý and published by the Brno University of Technology [here](https://www.vut.cz/en/students/final-thesis/detail/176466?zp_id=176466).

## Background
The 201k is a low cost FLCoS microdisplay available for a low cost on AliExpress. A datasheet can be found online at the [WhyCan Forum](https://whycan.com/t_2420.html).

**Resolution:** 300x224px

**Refresh Rate:** 60 Hz (360 Hz RGB field rate)


## Connection and Hardware Setup

**Connector:**
The module commonly available on AliExpress connects using a 25pin 0.5mm Pitch FPC. There are no readily available breakout boards for this size but the [Hirose FH12-25S-0.5SH](https://www.hirose.com/en/product/p/CL0586-0692-3-55) is compatible and available from various sources.

**Pinout:**

<img width="714" height="442" alt="22PIN25PIN" src="https://github.com/user-attachments/assets/ad70c652-8233-4d4d-b56a-a2cc92c13805" />

    ========== DISPLAY INTERFACE ==========
    PCLK   (Pixel Clock)        (default: 18)
    VD     (VSync)              (default: 3)
    HD     (HSync)              (default: 8)
    VALID  (Data Enable)        (N\A)
    D0-D7  (Data Bus)           (defaults: 9,10,11,12,13,14,21,47)
    nRST   (Active low enable)  (default: 46)

    ==========   SPI INTERFACE   ==========
    SEN     (SPI chip select)   (default: 40)
    SCLK    (SPI clock)         (default: 39)
    SDAT    (SPI MOSI)          (default: 48)
    SOUT    (SPI MISO)          (default: 38)

    ==========  POWER MANAGEMENT  ==========
    VCC/LED enable, active high (default: -1)
