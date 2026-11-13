// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
//
// The p32firmware.com web server: the generated RadioFoundry/P32 site, its flasher, and the endpoints the page's
// optional flash log and problem reports post to. The flasher runs entirely in the browser (Web Serial); this server
// never sees the radio, the firmware a user picks, or a backup.
package main

import (
	"crypto/tls"
	"embed"
	"flag"
	"io"
	"io/fs"
	"log"
	"net"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"strings"
	"syscall"
	"time"

	"golang.org/x/crypto/acme/autocert"
)

//go:embed site
var siteFS embed.FS

const maxReportBody = 1 << 20 // 1 MiB: flash-log rows and problem reports are small
const maxSampleBody = 8 << 20 // 8 MiB: the page offers a firmware file up to 4 MB

func env(key, def string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return def
}

func main() {
	var (
		dev      = flag.String("dev", "", "serve plain HTTP on this address (development; no Let's Encrypt)")
		domain   = flag.String("domain", env("P32_DOMAIN", "p32firmware.com"), "the site's domain")
		email    = flag.String("email", env("P32_EMAIL", "sarahroselives@protonmail.com"), "Let's Encrypt account email")
		cache    = flag.String("cache", env("P32_CACHE", "/var/lib/p32firmware/autocert"), "Let's Encrypt certificate cache")
		reports  = flag.String("reports", env("P32_REPORTS", "/var/lib/p32firmware/reports"), "directory for the page's problem reports and flash log (empty: discard)")
		httpAdr  = flag.String("http", env("P32_HTTP_ADDR", ":80"), "ACME challenge and HTTP-to-HTTPS address")
		httpsAdr = flag.String("https", env("P32_HTTPS_ADDR", ":443"), "HTTPS address")
	)
	flag.Parse()

	if *reports != "" {
		if err := os.MkdirAll(*reports, 0o700); err != nil {
			log.Printf("reports directory %s: %v (reports will be logged to the journal only)", *reports, err)
		}
	}
	h := handler(*domain, *reports)

	if *dev != "" {
		log.Printf("development server on http://%s", *dev)
		if err := http.ListenAndServe(*dev, h); err != nil {
			log.Fatal(err)
		}
		return
	}

	if err := os.MkdirAll(*cache, 0o700); err != nil {
		log.Fatalf("certificate cache %s: %v", *cache, err)
	}
	m := &autocert.Manager{
		Prompt:     autocert.AcceptTOS,
		HostPolicy: autocert.HostWhitelist(*domain, "www."+*domain),
		Cache:      autocert.DirCache(*cache),
		Email:      *email,
	}

	// Port 80: answer the ACME HTTP-01 challenge, send everything else to HTTPS.
	go func() {
		srv := &http.Server{
			Addr:              *httpAdr,
			Handler:           m.HTTPHandler(http.HandlerFunc(redirectHTTPS)),
			ReadHeaderTimeout: 10 * time.Second,
		}
		log.Printf("HTTP on %s (ACME and redirect)", *httpAdr)
		if err := srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatalf("http: %v", err)
		}
	}()

	srv := &http.Server{
		Addr:              *httpsAdr,
		Handler:           h,
		ReadHeaderTimeout: 10 * time.Second,
		TLSConfig: &tls.Config{
			MinVersion:     tls.VersionTLS12,
			GetCertificate: m.GetCertificate,
			NextProtos:     []string{"h2", "http/1.1", "acme-tls/1"},
		},
	}
	go func() {
		sig := make(chan os.Signal, 1)
		signal.Notify(sig, syscall.SIGINT, syscall.SIGTERM)
		<-sig
		log.Print("shutting down")
		_ = srv.Close()
	}()

	log.Printf("HTTPS on %s for %s (Let's Encrypt account %s)", *httpsAdr, *domain, *email)
	if err := srv.ListenAndServeTLS("", ""); err != nil && err != http.ErrServerClosed {
		log.Fatalf("https: %v", err)
	}
}

func redirectHTTPS(w http.ResponseWriter, r *http.Request) {
	host := r.Host
	if i := strings.IndexByte(host, ':'); i >= 0 {
		host = host[:i]
	}
	target := "https://" + host + r.URL.RequestURI()
	http.Redirect(w, r, target, http.StatusPermanentRedirect)
}

func handler(domain, reports string) http.Handler {
	sub, err := fs.Sub(siteFS, "site")
	if err != nil {
		log.Fatal(err)
	}
	fileServer := http.FileServer(http.FS(sub))

	mux := http.NewServeMux()
	mux.HandleFunc("/healthz", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/plain; charset=utf-8")
		io.WriteString(w, "ok\n")
	})
	mux.HandleFunc("/v1/", func(w http.ResponseWriter, r *http.Request) { api(w, r, reports) })
	mux.Handle("/", fileServer)

	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		// The apex is canonical; anything on www goes there before the page (or a Web Serial grant) is used.
		host := r.Host
		if i := strings.IndexByte(host, ':'); i >= 0 {
			host = host[:i]
		}
		if strings.HasPrefix(host, "www.") {
			http.Redirect(w, r, "https://"+domain+r.URL.RequestURI(), http.StatusPermanentRedirect)
			return
		}
		w.Header().Set("X-Content-Type-Options", "nosniff")
		w.Header().Set("X-Frame-Options", "DENY")
		w.Header().Set("Referrer-Policy", "no-referrer")
		w.Header().Set("Strict-Transport-Security", "max-age=31536000; includeSubDomains")
		w.Header().Set("Permissions-Policy", "camera=(), microphone=(), geolocation=()")
		if r.URL.Path == "/flash" || r.URL.Path == "/flash/" || r.URL.Path == "/flash/index.html" {
			http.Redirect(w, r, "https://"+domain+"/install/", http.StatusPermanentRedirect)
			return
		}
		switch {
		case r.URL.Path == "/" || r.URL.Path == "/index.html" ||
			r.URL.Path == "/install" || r.URL.Path == "/install/" || r.URL.Path == "/install/index.html":
			w.Header().Set("Cache-Control", "no-store")
			w.Header().Set("Clear-Site-Data", "\"cache\"")
		case strings.HasPrefix(r.URL.Path, "/static/js/") || strings.HasPrefix(r.URL.Path, "/static/css/") ||
			strings.HasPrefix(r.URL.Path, "/js/") || strings.HasPrefix(r.URL.Path, "/css/"):
			// The page's modules import each other by path, so a version query on app.js would not reach them: revalidate
			// every time instead, and a deploy takes effect on the next load.
			w.Header().Set("Cache-Control", "no-store")
		case strings.HasPrefix(r.URL.Path, "/img/"):
			w.Header().Set("Cache-Control", "public, max-age=86400")
		case strings.HasPrefix(r.URL.Path, "/firmware/") && strings.HasSuffix(r.URL.Path, ".json"):
			// The manifest must never come from a cache; the page asks for the image under its SHA-256, so no cached
			// image can be paired with a different manifest.
			w.Header().Set("Cache-Control", "no-cache")
		case strings.HasPrefix(r.URL.Path, "/firmware/"):
			w.Header().Set("Cache-Control", "public, max-age=600")
		case strings.HasPrefix(r.URL.Path, "/data/"):
			w.Header().Set("Cache-Control", "no-cache")
		}
		log.Printf("%s %s %s", clientIP(r), r.Method, r.URL.Path)
		mux.ServeHTTP(w, r)
	})
}

func clientIP(r *http.Request) string {
	if host, _, err := net.SplitHostPort(r.RemoteAddr); err == nil {
		return host
	}
	return r.RemoteAddr
}

// api takes the page's optional flash log, problem reports and sample uploads. Reports and flash-log rows are kept in
// the reports directory so a failed write can be diagnosed; samples (a firmware the page could not identify) are
// accepted and discarded. The page's error paths work whether or not the directory is writable.
func api(w http.ResponseWriter, r *http.Request, reports string) {
	limit := int64(maxReportBody)
	if strings.HasPrefix(r.URL.Path, "/v1/samples") {
		limit = maxSampleBody
	}
	r.Body = http.MaxBytesReader(w, r.Body, limit)
	body, _ := io.ReadAll(r.Body)
	if reports != "" && !strings.HasPrefix(r.URL.Path, "/v1/samples") {
		keep := strings.HasPrefix(r.URL.Path, "/v1/reports") || strings.HasPrefix(r.URL.Path, "/v1/flashes")
		if keep {
			line := time.Now().UTC().Format(time.RFC3339) + " " + clientIP(r) + " " + r.Method + " " +
				r.URL.RequestURI() + "\n" + string(body) + "\n"
			if err := appendReport(filepath.Join(reports, "events.log"), line); err != nil {
				log.Printf("reports: %v", err)
			}
			log.Printf("REPORT %s %s %s %s", clientIP(r), r.Method, r.URL.Path, string(body))
		}
	}
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")
	switch {
	case r.Method == http.MethodPost && strings.HasPrefix(r.URL.Path, "/v1/samples"):
		io.WriteString(w, `{"status":"accepted"}`)
	case r.Method == http.MethodPost || r.Method == http.MethodPatch:
		io.WriteString(w, "{}")
	default:
		w.WriteHeader(http.StatusNoContent)
	}
}

func appendReport(path, line string) error {
	f, err := os.OpenFile(path, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0o600)
	if err != nil {
		return err
	}
	defer f.Close()
	_, err = f.WriteString(line)
	return err
}
