package main

import (
	"bytes"
	"compress/gzip"
	"context"
	"crypto/md5"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"github.com/wailsapp/wails/v2/pkg/runtime"
	"go.bug.st/serial"
	"go.bug.st/serial/enumerator"
)

// espVID is the USB vendor ID of the XIAO ESP32-C3's built-in USB-Serial-JTAG.
const espVID = "303A"

// ghRepo is the GitHub repo queried for release auto-discovery.
const ghRepo = "mateusdemboski/SpoolID"

// App holds the serial connection and exposes bound methods to the frontend.
// Every method just moves a line-JSON command to the device and parses the
// single-line JSON reply — the firmware builds and encrypts everything.
type App struct {
	ctx  context.Context
	mu   sync.Mutex
	port serial.Port
}

// NewApp creates the application struct bound to the frontend.
func NewApp() *App { return &App{} }

// Version returns the desktop app's semantic version (set via -ldflags at
// release build). The frontend gates compatibility on the major.minor pair
// against the firmware version reported by getspec.
func (a *App) Version() string { return version }

// pushDB gzips a material_database.json and streams it to the device over serial
// (dbbegin -> dbdata -> dbend); the device stores it to LittleFS for its web UI.
func (a *App) pushDB(raw []byte) error {
	var buf bytes.Buffer
	gw := gzip.NewWriter(&buf)
	if _, err := gw.Write(raw); err != nil {
		return err
	}
	if err := gw.Close(); err != nil {
		return err
	}
	data := buf.Bytes()

	a.mu.Lock()
	defer a.mu.Unlock()

	begin, err := a.sendLocked(map[string]interface{}{"cmd": "dbbegin", "size": len(data)}, 10*time.Second)
	if err != nil {
		return fmt.Errorf("dbbegin: %w", err)
	}
	if ok, _ := begin["ok"].(bool); !ok {
		return fmt.Errorf("dbbegin rejected: %v", begin["error"])
	}

	const chunk = 4096 // matches OTA_CHUNK in the firmware
	for off := 0; off < len(data); off += chunk {
		end := off + chunk
		if end > len(data) {
			end = len(data)
		}
		b64 := base64.StdEncoding.EncodeToString(data[off:end])
		r, err := a.sendLocked(map[string]interface{}{"cmd": "dbdata", "b": b64}, 10*time.Second)
		if err != nil {
			return fmt.Errorf("dbdata @%d: %w", off, err)
		}
		if ok, _ := r["ok"].(bool); !ok {
			return fmt.Errorf("dbdata rejected: %v", r["error"])
		}
	}

	fin, err := a.sendLocked(map[string]interface{}{"cmd": "dbend"}, 10*time.Second)
	if err != nil {
		return fmt.Errorf("dbend: %w", err)
	}
	if ok, _ := fin["ok"].(bool); !ok {
		return fmt.Errorf("dbend rejected: %v", fin["error"])
	}
	return nil
}

// UploadDB opens a file picker for a material_database.json and pushes it to the
// device. Returns the uploaded file name, or "" if the picker was cancelled.
func (a *App) UploadDB() (string, error) {
	path, err := runtime.OpenFileDialog(a.ctx, runtime.OpenDialogOptions{
		Title:   "Select material_database.json",
		Filters: []runtime.FileFilter{{DisplayName: "JSON (*.json)", Pattern: "*.json"}},
	})
	if err != nil {
		return "", fmt.Errorf("file dialog: %w", err)
	}
	if path == "" {
		return "", nil // cancelled
	}
	raw, err := os.ReadFile(path)
	if err != nil {
		return "", fmt.Errorf("read: %w", err)
	}
	if err := a.pushDB(raw); err != nil {
		return "", err
	}
	return filepath.Base(path), nil
}

// PullDBFromPrinter fetches material_database.json from a Creality printer over
// the network (TLS/HTTP on the desktop) and pushes it to the device. host is
// "ip" or "ip:port".
func (a *App) PullDBFromPrinter(host string) error {
	url := "http://" + host + "/downloads/defData/material_database.json"
	resp, err := http.Get(url)
	if err != nil {
		return fmt.Errorf("printer: %w", err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("printer: HTTP %d", resp.StatusCode)
	}
	raw, err := io.ReadAll(resp.Body)
	if err != nil {
		return fmt.Errorf("printer: %w", err)
	}
	if len(raw) == 0 {
		return fmt.Errorf("printer returned an empty database")
	}
	return a.pushDB(raw)
}

// Release describes the latest GitHub release for update auto-discovery.
type Release struct {
	Version    string `json:"version"`    // tag without a leading "v"
	Tag        string `json:"tag"`        // the raw tag (vX.Y.Z)
	URL        string `json:"url"`        // release page (html_url)
	Firmware   string `json:"firmware"`   // firmware.bin download URL ("" if absent)
	Filesystem string `json:"filesystem"` // littlefs.bin download URL ("" if absent)
}

// CheckUpdate queries the GitHub Releases API for the latest release and returns
// its version + the firmware/filesystem asset URLs, so the UI can offer a
// one-click update instead of a pasted URL.
func (a *App) CheckUpdate() (*Release, error) {
	req, _ := http.NewRequest(http.MethodGet,
		"https://api.github.com/repos/"+ghRepo+"/releases/latest", nil)
	req.Header.Set("Accept", "application/vnd.github+json")
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return nil, fmt.Errorf("github: %w", err)
	}
	defer resp.Body.Close()
	if resp.StatusCode == http.StatusNotFound {
		return nil, fmt.Errorf("no releases published yet")
	}
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("github: HTTP %d", resp.StatusCode)
	}

	var gh struct {
		TagName string `json:"tag_name"`
		HTMLURL string `json:"html_url"`
		Assets  []struct {
			Name string `json:"name"`
			URL  string `json:"browser_download_url"`
		} `json:"assets"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&gh); err != nil {
		return nil, fmt.Errorf("github: %w", err)
	}

	rel := &Release{Tag: gh.TagName, Version: strings.TrimPrefix(gh.TagName, "v"), URL: gh.HTMLURL}
	for _, as := range gh.Assets {
		switch as.Name {
		case "firmware.bin":
			rel.Firmware = as.URL
		case "littlefs.bin":
			rel.Filesystem = as.URL
		}
	}
	return rel, nil
}

func (a *App) startup(ctx context.Context) { a.ctx = ctx }

func (a *App) shutdown(_ context.Context) {
	a.mu.Lock()
	defer a.mu.Unlock()
	a.closeLocked()
}

func (a *App) closeLocked() {
	if a.port != nil {
		_ = a.port.Close()
		a.port = nil
	}
}

// PortInfo describes a serial port for the picker. IsDevice flags a likely
// SpoolID writer (the XIAO ESP32-C3 USB-Serial-JTAG), so the UI can label it and
// auto-connect when it's the only one.
type PortInfo struct {
	Name     string `json:"name"`
	Label    string `json:"label"`
	IsDevice bool   `json:"isDevice"`
}

// ListPorts returns the available serial ports with friendly labels, flagging
// the ones that look like a SpoolID device (by USB VID).
func (a *App) ListPorts() ([]PortInfo, error) {
	ports, err := enumerator.GetDetailedPortsList()
	if err != nil {
		return nil, fmt.Errorf("enumerate ports: %w", err)
	}
	out := make([]PortInfo, 0, len(ports))
	for _, p := range ports {
		info := PortInfo{Name: p.Name, Label: p.Name}
		if p.IsUSB {
			if strings.EqualFold(p.VID, espVID) {
				info.IsDevice = true
				info.Label = "SpoolID device — " + p.Name
			} else if p.Product != "" {
				info.Label = p.Product + " — " + p.Name
			}
		}
		out = append(out, info)
	}
	return out, nil
}

// Connect opens a serial port. DTR/RTS are deasserted so the ESP32-C3
// USB-Serial-JTAG isn't held in reset.
func (a *App) Connect(port string, baud int) error {
	a.mu.Lock()
	defer a.mu.Unlock()
	a.closeLocked()

	p, err := serial.Open(port, &serial.Mode{BaudRate: baud})
	if err != nil {
		return fmt.Errorf("open %s: %w", port, err)
	}
	if err := p.SetReadTimeout(300 * time.Millisecond); err != nil {
		_ = p.Close()
		return fmt.Errorf("set timeout: %w", err)
	}
	_ = p.SetDTR(false)
	_ = p.SetRTS(false)
	time.Sleep(300 * time.Millisecond)
	_ = p.ResetInputBuffer()
	a.port = p
	return nil
}

// Disconnect closes the current serial port, if any.
func (a *App) Disconnect() error {
	a.mu.Lock()
	defer a.mu.Unlock()
	a.closeLocked()
	return nil
}

// Send writes one JSON command line and returns the device's JSON reply.
func (a *App) Send(cmd map[string]interface{}) (map[string]interface{}, error) {
	a.mu.Lock()
	defer a.mu.Unlock()
	return a.sendLocked(cmd, 8*time.Second)
}

// sendLocked is the request/response core; the caller must hold a.mu. Firmware
// log lines start with '['; only a line starting with '{' is a reply.
func (a *App) sendLocked(cmd map[string]interface{}, timeout time.Duration) (map[string]interface{}, error) {
	if a.port == nil {
		return nil, fmt.Errorf("not connected")
	}

	line, err := json.Marshal(cmd)
	if err != nil {
		return nil, err
	}
	_ = a.port.ResetInputBuffer()
	if _, err := a.port.Write(append(line, '\n')); err != nil {
		return nil, fmt.Errorf("write: %w", err)
	}

	deadline := time.Now().Add(timeout)
	buf := make([]byte, 512)
	var acc []byte
	for time.Now().Before(deadline) {
		n, err := a.port.Read(buf)
		if err != nil {
			return nil, fmt.Errorf("read: %w", err)
		}
		if n == 0 {
			continue // read timeout; keep waiting until the deadline
		}
		acc = append(acc, buf[:n]...)
		for {
			i := bytes.IndexByte(acc, '\n')
			if i < 0 {
				break
			}
			raw := bytes.TrimSpace(acc[:i])
			acc = acc[i+1:]
			if len(raw) == 0 || raw[0] != '{' {
				continue // log line or noise
			}
			var reply map[string]interface{}
			if err := json.Unmarshal(raw, &reply); err != nil {
				continue
			}
			return reply, nil
		}
	}
	return nil, fmt.Errorf("no JSON response from device")
}

// FlashFirmware downloads an image (TLS handled here on the desktop) and relays
// it to the device over serial: otabegin -> otadata(base64) * N -> otaend. The
// device self-flashes via ESP Update (MD5-verified, A/B rollback). Progress is
// emitted as the "ota:progress" event (0..1). `filesystem` targets the LittleFS
// image instead of the app. The device reboots on success, so the port is
// dropped for the UI to reconnect.
func (a *App) FlashFirmware(url string, filesystem bool) error {
	runtime.EventsEmit(a.ctx, "ota:progress", 0.0)

	resp, err := http.Get(url)
	if err != nil {
		return fmt.Errorf("download: %w", err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("download: HTTP %d", resp.StatusCode)
	}
	data, err := io.ReadAll(resp.Body)
	if err != nil {
		return fmt.Errorf("download: %w", err)
	}
	if len(data) == 0 {
		return fmt.Errorf("empty image")
	}
	sum := md5.Sum(data)
	md5hex := hex.EncodeToString(sum[:])

	target := "app"
	if filesystem {
		target = "fs"
	}

	a.mu.Lock()
	defer a.mu.Unlock()

	begin, err := a.sendLocked(map[string]interface{}{
		"cmd": "otabegin", "size": len(data), "target": target, "md5": md5hex,
	}, 10*time.Second)
	if err != nil {
		return fmt.Errorf("otabegin: %w", err)
	}
	if ok, _ := begin["ok"].(bool); !ok {
		return fmt.Errorf("otabegin rejected: %v", begin["error"])
	}

	const chunk = 4096 // matches OTA_CHUNK in the firmware
	for off := 0; off < len(data); off += chunk {
		end := off + chunk
		if end > len(data) {
			end = len(data)
		}
		b64 := base64.StdEncoding.EncodeToString(data[off:end])
		r, err := a.sendLocked(map[string]interface{}{"cmd": "otadata", "b": b64}, 10*time.Second)
		if err != nil {
			return fmt.Errorf("otadata @%d: %w", off, err)
		}
		if ok, _ := r["ok"].(bool); !ok {
			return fmt.Errorf("otadata rejected: %v", r["error"])
		}
		runtime.EventsEmit(a.ctx, "ota:progress", float64(end)/float64(len(data)))
	}

	fin, err := a.sendLocked(map[string]interface{}{"cmd": "otaend"}, 20*time.Second)
	if err != nil {
		return fmt.Errorf("otaend: %w", err)
	}
	if ok, _ := fin["ok"].(bool); !ok {
		return fmt.Errorf("otaend rejected: %v", fin["error"])
	}

	a.closeLocked() // device reboots + re-enumerates USB; UI reconnects
	runtime.EventsEmit(a.ctx, "ota:progress", 1.0)
	return nil
}
