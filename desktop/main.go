package main

import (
	"embed"
	"log"

	"github.com/wailsapp/wails/v2"
	"github.com/wailsapp/wails/v2/pkg/options"
	"github.com/wailsapp/wails/v2/pkg/options/assetserver"
)

//go:embed all:frontend/dist
var assets embed.FS

// version is the app's semantic version. Release CI overrides it via
// -ldflags "-X main.version=X.Y.Z"; local builds keep the dev fallback.
var version = "0.0.0-dev"

// SpoolID desktop app: a thin serial client for the ESP32-C3 writer. The Go
// backend owns only the USB link + the embedded material DB; the firmware
// remains the source of truth for crypto/format/logic. UI is web tech served
// from the embedded frontend build.
func main() {
	app := NewApp()

	err := wails.Run(&options.App{
		Title:            "SpoolID",
		Width:            600,
		Height:           895,
		MinWidth:         480,
		MinHeight:        640,
		BackgroundColour: &options.RGBA{R: 27, G: 27, B: 27, A: 255},
		AssetServer:      &assetserver.Options{Assets: assets},
		OnStartup:        app.startup,
		OnShutdown:       app.shutdown,
		Bind:             []interface{}{app},
	})
	if err != nil {
		log.Fatal(err)
	}
}
