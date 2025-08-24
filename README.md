# Networks-Lab Assignment1 (Windows)

> **Developer Command Prompt for VS**

```cd C:\01_Arjeesh_Drive\CN```

```cl /EHsc /std:c++17 server.cpp Ws2_32.lib```
```cl /EHsc /std:c++17 client.cpp Ws2_32.lib```

> **VS Code**

Open 2 terminals
```cd C:\01_Arjeesh_Drive\CN```
> 
```Server.exe 5000```
```client.exe 127.0.0.1 5000 msg.bits --scheme crc16 --inject no```

> **Formats**
`--scheme` options
| Scheme     | Description                              |
| ---------- | ---------------------------------------- |
| checksum16 | 16-bit one’s-complement checksum (basic) |
| crc8       | CRC-8 (ATM)                              |
| crc10      | CRC-10                                   |
| crc16      | CRC-16 (IBM)                             |
| crc32      | CRC-32 (IEEE 802.3)                      |

`--injectscheme` values
| Value | Name          | Effect                                |
| ----: | ------------- | ------------------------------------- |
|     0 | SINGLE\_BIT   | Flips exactly one random bit          |
|     1 | TWO\_ISOLATED | Flips two bits far apart              |
|     2 | ODD\_ERRORS   | Flips 1 or 3 or 5 random bits         |
|     3 | BURST         | Flips many bits within a short window |


