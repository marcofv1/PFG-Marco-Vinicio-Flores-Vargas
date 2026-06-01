"""
SISTEMA DE PONCHADO AUTOMATICO
Interfaz grafica de operacion
Proyecto de Graduacion - Marco Flores - TEC 2026

Requisitos:
    pip install pyserial

Uso:
    python ponchado_gui.py

El scanner de codigo de barras debe estar conectado por USB
y configurado para terminar cada lectura con Enter.
La interfaz envia comandos al Teensy 4.1 por puerto serial.
"""

import tkinter as tk
from tkinter import ttk, font, messagebox
import serial
import serial.tools.list_ports
import threading
import queue
import time
import re

# ─────────────────────────────────────────────
#  CONSTANTES
# ─────────────────────────────────────────────

APP_TITLE   = "Sistema de Ponchado Automatico"
BAUD_RATE   = 115200
POLL_MS     = 50          # ms entre lecturas del buffer serial
LOG_MAX     = 500         # lineas maximas en el log

# Colores del tema industrial
C_BG        = "#1a1a1a"
C_PANEL     = "#242424"
C_BORDER    = "#333333"
C_TEXT      = "#e8e8e8"
C_MUTED     = "#888888"
C_ACCENT    = "#00a8ff"
C_GREEN     = "#00c853"
C_RED       = "#ff3d3d"
C_YELLOW    = "#ffd600"
C_ORANGE    = "#ff6d00"

# Mapeo de estados recibidos del Teensy
STATE_COLORS = {
    "HOMING":        C_YELLOW,
    "ESPERANDO":     C_ACCENT,
    "MOVIENDO_MESA": C_ORANGE,
    "PERFORANDO":    C_GREEN,
    "FIN":           C_GREEN,
    "ERROR":         C_RED,
    "---":           C_MUTED,
}

# ─────────────────────────────────────────────
#  CLASE PRINCIPAL
# ─────────────────────────────────────────────

class PonchadoApp(tk.Tk):

    def __init__(self):
        super().__init__()

        self.title(APP_TITLE)
        self.configure(bg=C_BG)
        self.resizable(False, False)

        # Estado interno
        self.serial_conn   = None
        self.serial_thread = None
        self.running       = False
        self.rx_queue      = queue.Queue()

        self.estado_var    = tk.StringVar(value="---")
        self.parte_var     = tk.StringVar(value="---")
        self.agujero_var   = tk.StringVar(value="0 / 0")
        self.port_var      = tk.StringVar()
        self.scanner_input = ""   # buffer para el scanner HID

        self._build_ui()
        self._refresh_ports()
        self._poll_serial()

        # Captura global de teclas del scanner (dispositivo HID = teclado)
        self.bind("<Key>",       self._on_key)
        self.bind("<Return>",    self._on_scanner_enter)
        self.protocol("WM_DELETE_WINDOW", self._on_close)

    # ─────────────────────────────────────────
    #  CONSTRUCCION DE UI
    # ─────────────────────────────────────────

    def _build_ui(self):

        pad = {"padx": 12, "pady": 8}

        # ── Encabezado ──────────────────────────────────────
        header = tk.Frame(self, bg=C_BG)
        header.pack(fill="x", padx=16, pady=(14, 4))

        tk.Label(header, text="PONCHADO AUTOMATICO",
                 bg=C_BG, fg=C_TEXT,
                 font=("Courier New", 16, "bold")).pack(side="left")

        tk.Label(header, text="Abbott Medical · TEC 2026",
                 bg=C_BG, fg=C_MUTED,
                 font=("Courier New", 9)).pack(side="right", pady=4)

        tk.Frame(self, bg=C_BORDER, height=1).pack(fill="x", padx=16)

        # ── Cuerpo principal ────────────────────────────────
        body = tk.Frame(self, bg=C_BG)
        body.pack(fill="both", padx=16, pady=10)

        left  = tk.Frame(body, bg=C_BG)
        right = tk.Frame(body, bg=C_BG)
        left.pack(side="left", fill="y", padx=(0, 10))
        right.pack(side="left", fill="both", expand=True)

        # ── Panel de estado ─────────────────────────────────
        self._build_status_panel(left)

        # ── Panel de conexion ───────────────────────────────
        self._build_connection_panel(left)

        # ── Controles ───────────────────────────────────────
        self._build_controls(left)

        # ── Log ─────────────────────────────────────────────
        self._build_log(right)

    def _panel(self, parent, title):
        """Crea un frame con borde y titulo."""
        outer = tk.Frame(parent, bg=C_BORDER, bd=0)
        outer.pack(fill="x", pady=(0, 10))
        inner = tk.Frame(outer, bg=C_PANEL, bd=0)
        inner.pack(fill="x", padx=1, pady=1)
        tk.Label(inner, text=f"  {title}",
                 bg=C_PANEL, fg=C_MUTED,
                 font=("Courier New", 8, "bold"),
                 anchor="w").pack(fill="x", padx=8, pady=(6, 2))
        tk.Frame(inner, bg=C_BORDER, height=1).pack(fill="x", padx=8)
        content = tk.Frame(inner, bg=C_PANEL)
        content.pack(fill="x", padx=10, pady=8)
        return content

    def _build_status_panel(self, parent):
        p = self._panel(parent, "ESTADO DEL SISTEMA")

        # Estado
        row = tk.Frame(p, bg=C_PANEL)
        row.pack(fill="x", pady=2)
        tk.Label(row, text="Estado",  bg=C_PANEL, fg=C_MUTED,
                 font=("Courier New", 9), width=12, anchor="w").pack(side="left")
        self.estado_lbl = tk.Label(row, textvariable=self.estado_var,
                                   bg=C_PANEL, fg=C_MUTED,
                                   font=("Courier New", 11, "bold"), anchor="w")
        self.estado_lbl.pack(side="left")

        # Parte activa
        row2 = tk.Frame(p, bg=C_PANEL)
        row2.pack(fill="x", pady=2)
        tk.Label(row2, text="Parte",  bg=C_PANEL, fg=C_MUTED,
                 font=("Courier New", 9), width=12, anchor="w").pack(side="left")
        tk.Label(row2, textvariable=self.parte_var,
                 bg=C_PANEL, fg=C_TEXT,
                 font=("Courier New", 11, "bold"), anchor="w").pack(side="left")

        # Progreso agujeros
        row3 = tk.Frame(p, bg=C_PANEL)
        row3.pack(fill="x", pady=2)
        tk.Label(row3, text="Agujero",  bg=C_PANEL, fg=C_MUTED,
                 font=("Courier New", 9), width=12, anchor="w").pack(side="left")
        tk.Label(row3, textvariable=self.agujero_var,
                 bg=C_PANEL, fg=C_TEXT,
                 font=("Courier New", 11, "bold"), anchor="w").pack(side="left")

        # Barra de progreso
        tk.Frame(p, bg=C_PANEL, height=6).pack()
        self.progress = ttk.Progressbar(p, orient="horizontal",
                                        length=240, mode="determinate")
        self.progress.pack(fill="x")
        self._style_progressbar()

        # Indicador scanner
        tk.Frame(p, bg=C_PANEL, height=4).pack()
        scan_row = tk.Frame(p, bg=C_PANEL)
        scan_row.pack(fill="x")
        tk.Label(scan_row, text="Scanner",  bg=C_PANEL, fg=C_MUTED,
                 font=("Courier New", 9), width=12, anchor="w").pack(side="left")
        self.scan_lbl = tk.Label(scan_row, text="esperando...",
                                  bg=C_PANEL, fg=C_MUTED,
                                  font=("Courier New", 9), anchor="w")
        self.scan_lbl.pack(side="left")

    def _style_progressbar(self):
        s = ttk.Style()
        s.theme_use("default")
        s.configure("green.Horizontal.TProgressbar",
                    troughcolor=C_BORDER,
                    background=C_GREEN,
                    bordercolor=C_BORDER,
                    lightcolor=C_GREEN,
                    darkcolor=C_GREEN)
        self.progress.configure(style="green.Horizontal.TProgressbar")

    def _build_connection_panel(self, parent):
        p = self._panel(parent, "CONEXION SERIAL")

        # Puerto
        port_row = tk.Frame(p, bg=C_PANEL)
        port_row.pack(fill="x", pady=2)
        tk.Label(port_row, text="Puerto", bg=C_PANEL, fg=C_MUTED,
                 font=("Courier New", 9), width=8, anchor="w").pack(side="left")
        self.port_combo = ttk.Combobox(port_row, textvariable=self.port_var,
                                       width=14, state="readonly",
                                       font=("Courier New", 9))
        self.port_combo.pack(side="left", padx=(4, 4))
        tk.Button(port_row, text="↺",
                  bg=C_BORDER, fg=C_TEXT,
                  font=("Courier New", 9),
                  relief="flat", bd=0, padx=6,
                  cursor="hand2",
                  command=self._refresh_ports).pack(side="left")

        # Boton conectar
        self.connect_btn = tk.Button(p, text="CONECTAR",
                                     bg=C_ACCENT, fg="#000000",
                                     font=("Courier New", 9, "bold"),
                                     relief="flat", bd=0,
                                     padx=10, pady=5,
                                     cursor="hand2",
                                     command=self._toggle_connection)
        self.connect_btn.pack(fill="x", pady=(6, 0))

    def _build_controls(self, parent):
        p = self._panel(parent, "CONTROL DE PROCESO")

        btn_cfg = [
            ("START",  C_GREEN,  "#000",  self._cmd_start),
            ("STOP",   C_RED,    "#fff",  self._cmd_stop),
            ("RESET",  C_YELLOW, "#000",  self._cmd_reset),
            ("STATUS", C_BORDER, C_TEXT,  self._cmd_status),
        ]

        for label, bg, fg, cmd in btn_cfg:
            b = tk.Button(p, text=label,
                          bg=bg, fg=fg,
                          font=("Courier New", 10, "bold"),
                          relief="flat", bd=0,
                          padx=10, pady=7,
                          cursor="hand2",
                          command=cmd)
            b.pack(fill="x", pady=2)

    def _build_log(self, parent):
        lf = self._panel(parent, "MONITOR SERIAL")

        self.log_text = tk.Text(lf,
                                bg="#111111", fg=C_TEXT,
                                font=("Courier New", 9),
                                width=52, height=28,
                                relief="flat", bd=0,
                                state="disabled",
                                wrap="word")
        sb = tk.Scrollbar(lf, command=self.log_text.yview,
                          bg=C_BORDER, troughcolor=C_PANEL,
                          relief="flat")
        self.log_text.configure(yscrollcommand=sb.set)

        self.log_text.pack(side="left", fill="both", expand=True)
        sb.pack(side="right", fill="y")

        # Tags de color para el log
        self.log_text.tag_configure("info",    foreground=C_TEXT)
        self.log_text.tag_configure("error",   foreground=C_RED)
        self.log_text.tag_configure("warn",    foreground=C_YELLOW)
        self.log_text.tag_configure("ok",      foreground=C_GREEN)
        self.log_text.tag_configure("sent",    foreground=C_ACCENT)
        self.log_text.tag_configure("muted",   foreground=C_MUTED)
        self.log_text.tag_configure("scan",    foreground=C_ORANGE)

    # ─────────────────────────────────────────
    #  CONEXION SERIAL
    # ─────────────────────────────────────────

    def _refresh_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.port_combo["values"] = ports
        if ports:
            self.port_var.set(ports[0])

    def _toggle_connection(self):
        if self.serial_conn and self.serial_conn.is_open:
            self._disconnect()
        else:
            self._connect()

    def _connect(self):
        port = self.port_var.get()
        if not port:
            messagebox.showerror("Error", "Seleccione un puerto serial.")
            return
        try:
            self.serial_conn = serial.Serial(port, BAUD_RATE, timeout=0.1)
            self.running     = True
            self.serial_thread = threading.Thread(target=self._read_serial,
                                                   daemon=True)
            self.serial_thread.start()
            self.connect_btn.configure(text="DESCONECTAR", bg=C_RED, fg="#fff")
            self._log(f"Conectado a {port} @ {BAUD_RATE} baud", "ok")
        except serial.SerialException as e:
            messagebox.showerror("Error de conexion", str(e))

    def _disconnect(self):
        self.running = False
        if self.serial_conn:
            try:
                self.serial_conn.close()
            except Exception:
                pass
            self.serial_conn = None
        self.connect_btn.configure(text="CONECTAR", bg=C_ACCENT, fg="#000")
        self._log("Desconectado", "warn")

    # ─────────────────────────────────────────
    #  LECTURA SERIAL (hilo secundario)
    # ─────────────────────────────────────────

    def _read_serial(self):
        while self.running and self.serial_conn and self.serial_conn.is_open:
            try:
                line = self.serial_conn.readline()
                if line:
                    decoded = line.decode("utf-8", errors="replace").strip()
                    if decoded:
                        self.rx_queue.put(decoded)
            except Exception:
                break

    # ─────────────────────────────────────────
    #  POLLING (hilo principal, cada POLL_MS)
    # ─────────────────────────────────────────

    def _poll_serial(self):
        try:
            while not self.rx_queue.empty():
                line = self.rx_queue.get_nowait()
                self._process_rx(line)
        except queue.Empty:
            pass
        self.after(POLL_MS, self._poll_serial)

    def _process_rx(self, line: str):
        """Parsea una linea recibida del Teensy y actualiza la UI."""

        # Determinar tag visual
        tag = "info"
        if "[ERROR]" in line:
            tag = "error"
        elif "[WARN]" in line:
            tag = "warn"
        elif "OK" in line or "TERMINADO" in line or "completa" in line.lower():
            tag = "ok"
        elif line.startswith("===") or line.startswith("---"):
            tag = "muted"

        self._log(f"← {line}", tag)

        # ── Actualizar estado ──────────────────────────────
        # "[INFO]  Estado   : HOMING"
        m = re.search(r"Estado\s*:\s*(\w+)", line)
        if m:
            self._set_estado(m.group(1))

        # ── Actualizar parte ───────────────────────────────
        # "  Parte    : PARTE1"  o  "Parte activa : PARTE1"
        m = re.search(r"Parte\s*(?:activa|seleccionada)?\s*:\s*(\S+)", line, re.IGNORECASE)
        if m:
            self.parte_var.set(m.group(1))

        # ── Actualizar agujero ─────────────────────────────
        # "[INFO]  Agujero  : 3/9"
        # Captura: "[INFO]  Agujero  : 3/9", "[INFO]  Agujero 3/9 ->", "[STATUS] ESTADO 3/9"
        m = re.search(r"Agujero[\s:]+([0-9]+)/([0-9]+)", line)
        if not m:
            m = re.search(r"\[STATUS\]\s+\w+\s+([0-9]+)/([0-9]+)", line)
        if m:
            actual = int(m.group(1))
            total  = int(m.group(2))
            self.agujero_var.set(f"{actual} / {total}")
            pct = (actual / total * 100) if total > 0 else 0
            self.progress["value"] = pct

        # ── Detectar estado desde lineas de proceso ────────
        if "HOMING" in line and "===" in line:
            self._set_estado("HOMING")
        if "Envie START" in line:
            self._set_estado("ESPERANDO")
        if "PROCESO TERMINADO" in line:
            self._set_estado("FIN")
            self.progress["value"] = 100
        if "[ERROR]" in line:
            self._set_estado("ERROR")

    def _set_estado(self, estado: str):
        self.estado_var.set(estado)
        color = STATE_COLORS.get(estado, C_MUTED)
        self.estado_lbl.configure(fg=color)

    # ─────────────────────────────────────────
    #  SCANNER DE CODIGO DE BARRAS (HID)
    # ─────────────────────────────────────────

    def _on_key(self, event):
        """Acumula caracteres del scanner."""
        if event.char and event.char.isprintable():
            self.scanner_input += event.char
            self.scan_lbl.configure(text=self.scanner_input, fg=C_ORANGE)

    def _on_scanner_enter(self, event):
        """Al presionar Enter (fin de lectura del scanner)."""
        raw = self.scanner_input.strip().upper()
        self.scanner_input = ""

        if not raw:
            return

        self._log(f"Scanner: {raw}", "scan")
        self.scan_lbl.configure(text=f"✓ {raw}", fg=C_GREEN)
        self.after(2000, lambda: self.scan_lbl.configure(
            text="esperando...", fg=C_MUTED))

        # Intentar mapear a numero de parte
        # El scanner envia el codigo del producto; buscar patron PARTEn
        # o enviar directamente si ya viene como PARTE1, PARTE2, etc.
        if re.match(r"^PARTE\d+$", raw):
            self._send(raw)
        else:
            # Codigo de barras real del producto: enviar tal cual
            # El Teensy lo recibe y busca en su tabla de recetas
            self._send(raw)

    # ─────────────────────────────────────────
    #  COMANDOS
    # ─────────────────────────────────────────

    def _send(self, cmd: str):
        if not self.serial_conn or not self.serial_conn.is_open:
            self._log("Sin conexion serial", "error")
            return
        try:
            self.serial_conn.write((cmd + "\n").encode("utf-8"))
            self._log(f"→ {cmd}", "sent")
        except serial.SerialException as e:
            self._log(f"Error al enviar: {e}", "error")

    def _cmd_start(self):
        self._send("START")

    def _cmd_stop(self):
        self._send("STOP")

    def _cmd_reset(self):
        self._send("RESET")

    def _cmd_status(self):
        self._send("STATUS")

    # ─────────────────────────────────────────
    #  LOG
    # ─────────────────────────────────────────

    def _log(self, msg: str, tag: str = "info"):
        ts = time.strftime("%H:%M:%S")
        self.log_text.configure(state="normal")

        # Limitar lineas
        lines = int(self.log_text.index("end-1c").split(".")[0])
        if lines > LOG_MAX:
            self.log_text.delete("1.0", "50.0")

        self.log_text.insert("end", f"[{ts}] {msg}\n", tag)
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    # ─────────────────────────────────────────
    #  CIERRE
    # ─────────────────────────────────────────

    def _on_close(self):
        self._disconnect()
        self.destroy()


# ─────────────────────────────────────────────
#  ENTRY POINT
# ─────────────────────────────────────────────

if __name__ == "__main__":
    app = PonchadoApp()
    app.mainloop()
