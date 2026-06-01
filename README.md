# Sistema de Ponchado Automatizado
**Proyecto Final de Graduación — Ingeniería Mecatrónica**  
Instituto Tecnológico de Costa Rica  
Marco Vinicio Flores Vargas | 2019232427



## Contenido del repositorio
ponchado-automatico/
├── firmware/
│   └── ponchado_firmware.ino    # Firmware Teensy 4.1
├── gui/
│   └── ponchado_gui.py          # Interfaz gráfica Python
└── README.md

## Hardware

| Componente | Descripción |
|------------|-------------|
| Teensy 4.1 | Microcontrolador principal |
| Motor HBT57 Series | Servomotor híbrido de lazo cerrado × 2 |
| SFU1204 | Tornillo de bolas p=4mm × 2 |
| MGN12 | Riel guía lineal miniatura × 3 |
| LJ12A3-4-Z/BX | Sensor inductivo de presencia × 4 |
| PC817 | Módulo optoacoplador 8 canales |
| Fuente 36V 600W | Alimentación de motores |

## Firmware (Teensy 4.1)

### Requisitos
- [Arduino IDE](https://www.arduino.cc/en/software) con soporte para Teensy
- [Teensyduino](https://www.pjrc.com/teensy/teensyduino.html)

### Instalación
1. Abrir `firmware/ponchado_firmware.ino` en Arduino IDE
2. Seleccionar placa: **Teensy 4.1**
3. Seleccionar puerto COM correspondiente
4. Cargar el firmware

### Comandos serial (115200 baud)
| Comando | Descripción |
|---------|-------------|
| `START` | Inicia ciclo desde estado ESPERANDO |
| `STOP` | Parada de emergencia → estado ERROR |
| `RESET` | Rehace homing completo desde ERROR |
| `STATUS` | Muestra estado actual y agujero activo |
| `PARTEn` | Selecciona receta n (1–30) |

### Máquina de estados

HOMING → ESPERANDO → MOVIENDO_MESA → PERFORANDO → FIN
                ↑___________________________|
                           ERROR (desde cualquier estado via STOP)

### Recetas disponibles
El firmware incluye **30 recetas** correspondientes a los números de parte activos en producción, con configuraciones de 4, 8, 10 y 20 bandas en calibres French 4, 5 y 6.

## Interfaz gráfica (Python)

### Requisitos
```bash
pip install pyserial keyboard tkinter


### Uso
```bash
python gui/ponchado_gui.py


### Funcionalidad
- Detección automática de escaneo de código de barras (dispositivo HID)
- Envío automático del comando `PARTEn` al Teensy según el código escaneado
- Monitor serial en tiempo real
- Panel de estado con colores según estado del sistema
- Botones START, STOP, RESET y STATUS

### Configuración
En `ponchado_gui.py` ajustar:
```python
PUERTO_SERIAL = 'COM3'    # Puerto del Teensy (Windows) o '/dev/ttyACM0' (Linux)
BAUDRATE = 115200
BARCODE_TIMEOUT = 0.1     # Segundos entre caracteres del scanner


## Parámetros mecánicos clave

| Parámetro | Valor |
|-----------|-------|
| Resolución eje X | 400 pasos/mm |
| Resolución eje Y (agujas) | 400 pasos/mm |
| Overlap agujas/mesa | 600 pasos |
| Profundidad extra F6 | 4.5 mm |
| Profundidad extra F5 | 4.6 mm |
| Profundidad extra F4 | 4.7 mm |


## Licencia

Proyecto académico — Instituto Tecnológico de Costa Rica.  
Uso restringido a fines educativos y de investigación.

## Contacto

Marco Vinicio Flores Vargas  
Escuela de Ingeniería Mecatrónica — TEC  
