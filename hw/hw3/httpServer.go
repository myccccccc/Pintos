package main

import (
	"fmt"
	"io/ioutil"
	"net"
	"os"
	"os/signal"
	"strconv"
	"strings"
	"sync"

	"./http"
)

const usageMsg = "Usage: ./httpserver --files www_directory/ --port 8000 [--num-threads 5]\n" +
	"       ./httpserver --proxy inst.eecs.berkeley.edu:80 --port 8000 [--num-threads 5]\n"
const maxQueueSize = 50

var (
	serverFilesDirectory string
	proxyAddress         string
	proxyPort            int
	ch                   chan net.Conn
	workerGroup          sync.WaitGroup

	interrupted chan os.Signal
)

func notFoundRespound(connection net.Conn, FileAbsolutePath string) {
	http.StartResponse(connection, 404)
	http.SendHeader(connection, "Content-Type", "text/html")
	http.SendHeader(connection, "Server", "httpserver/1.0")
	http.EndHeaders(connection)
	http.SendString(connection, "<center>")
	http.SendString(connection, "<h1>404 NOTFOUND</h1>")
	http.SendString(connection, "<hr>")
	http.SendString(connection, "<p>")
	http.SendString(connection, FileAbsolutePath)
	http.SendString(connection, "</p>")
	http.SendString(connection, "</center>")
}

func fileRespound(connection net.Conn, FileAbsolutePath string) {
	fi, _ := os.Stat(FileAbsolutePath)
	http.StartResponse(connection, 200)
	http.SendHeader(connection, "Content-Type", http.GetMimeType(FileAbsolutePath))
	http.SendHeader(connection, "Content-Length", strconv.FormatInt(fi.Size(), 10))
	http.SendHeader(connection, "Server", "httpserver/1.0")
	http.EndHeaders(connection)
	content, _ := ioutil.ReadFile(FileAbsolutePath)
	http.SendData(connection, content)
}

func dirRespound(connection net.Conn, FileAbsolutePath string) {
	indexhtmlAbsolutePath := FileAbsolutePath + "/index.html"
	_, err := os.Stat(indexhtmlAbsolutePath)
	if os.IsNotExist(err) {
		http.StartResponse(connection, 200)
		http.SendHeader(connection, "Content-Type", "text/html")
		http.SendHeader(connection, "Server", "httpserver/1.0")
		http.EndHeaders(connection)
		fis, _ := ioutil.ReadDir(FileAbsolutePath)
		for _, fi := range fis {
			http.SendString(connection, "<a href=\"")
			http.SendString(connection, fi.Name())
			http.SendString(connection, "\">")
			http.SendString(connection, fi.Name())
			http.SendString(connection, "</a><br>")
		}
		http.SendString(connection, "<a href=\"")
		http.SendString(connection, ".")
		http.SendString(connection, "\">")
		http.SendString(connection, ".")
		http.SendString(connection, "</a><br>")
		http.SendString(connection, "<a href=\"")
		http.SendString(connection, "..")
		http.SendString(connection, "\">")
		http.SendString(connection, "..")
		http.SendString(connection, "</a><br>")
	} else {
		fileRespound(connection, indexhtmlAbsolutePath)
	}
}

func handleFilesRequest(connection net.Conn) {
	// TODO Fill this in to complete Task #2
	request, err := http.ParseRequest(connection)
	if err != nil {
		connection.Close()
		return
	}
	FileAbsolutePath := serverFilesDirectory + request.Path
	fi, err := os.Stat(FileAbsolutePath)
	if os.IsNotExist(err) {
		notFoundRespound(connection, FileAbsolutePath)
	} else {
		switch mode := fi.Mode(); {
		case mode.IsDir():
			dirRespound(connection, FileAbsolutePath)
		default:
			fileRespound(connection, FileAbsolutePath)
		}
	}
	connection.Close()
}

func dataTransfer(readConn net.Conn, writeConn net.Conn, wg *sync.WaitGroup) {
	buffer := make([]byte, 256)
	for {
		n, errRead := readConn.Read(buffer)
		if errRead != nil {
			break
		}
		_, errWrite := writeConn.Write(buffer[:n])
		if errWrite != nil {
			break
		}
	}
	wg.Done()
}

func handleProxyRequest(clientConn net.Conn) {
	// TODO Fill this in to complete Task #3
	// Open a connection to the specified upstream server
	// Create two goroutines to forward traffic
	// Use sync.WaitGroup to block until the goroutines have finished
	proxyConn, _ := net.Dial("tcp", proxyAddress+":"+strconv.Itoa(proxyPort))
	var wg sync.WaitGroup
	wg.Add(2)
	go dataTransfer(clientConn, proxyConn, &wg)
	go dataTransfer(proxyConn, clientConn, &wg)
	wg.Wait()
	proxyConn.Close()
	clientConn.Close()
}

func handleSigInt(Listener net.Listener) {
	// TODO Fill this in to help complete Task #4
	// You should run this in its own goroutine
	// Perform a blocking receive on the 'interrupted' channel
	// Then, clean up the main and worker goroutines
	// Finally, when all goroutines have cleanly finished, exit the process
	<-interrupted
	Listener.Close()
}

func initWorkerPool(numThreads int, requestHandler func(net.Conn)) {
	// TODO Fill this in as part of Task #1
	// Create a fixed number of goroutines to handle requests
	for i := 0; i < numThreads; i++ {
		workerGroup.Add(1)
		go Worker(requestHandler)
	}
}

func Worker(requestHandler func(net.Conn)) {
	for connection := range ch {
		requestHandler(connection)
	}
	workerGroup.Done()
}

func serveForever(numThreads int, port int, requestHandler func(net.Conn)) {
	// TODO Fill this in as part of Task #1
	// Create a Listener and accept client connections
	// Pass connections to the thread pool via a channel
	ch = make(chan net.Conn, maxQueueSize)
	initWorkerPool(numThreads, requestHandler)
	Listener, _ := net.Listen("tcp", ":"+strconv.Itoa(port))
	go handleSigInt(Listener)
	for {
		connection, err := Listener.Accept()
		if err != nil {
			break
		}
		ch <- connection
	}
	close(ch)
	workerGroup.Wait()
}

func exitWithUsage() {
	fmt.Fprintf(os.Stderr, usageMsg)
	os.Exit(-1)
}

func main() {
	// Command line argument parsing
	var requestHandler func(net.Conn)
	var serverPort int
	numThreads := 1
	var err error

	for i := 1; i < len(os.Args); i++ {
		switch os.Args[i] {
		case "--files":
			requestHandler = handleFilesRequest
			if i == len(os.Args)-1 {
				fmt.Fprintln(os.Stderr, "Expected argument after --files")
				exitWithUsage()
			}
			serverFilesDirectory = os.Args[i+1]
			i++

		case "--proxy":
			requestHandler = handleProxyRequest
			if i == len(os.Args)-1 {
				fmt.Fprintln(os.Stderr, "Expected argument after --proxy")
				exitWithUsage()
			}
			proxyTarget := os.Args[i+1]
			i++

			tokens := strings.SplitN(proxyTarget, ":", 2)
			proxyAddress = tokens[0]
			proxyPort, err = strconv.Atoi(tokens[1])
			if err != nil {
				fmt.Fprintln(os.Stderr, "Expected integer for proxy port")
				exitWithUsage()
			}

		case "--port":
			if i == len(os.Args)-1 {
				fmt.Fprintln(os.Stderr, "Expected argument after --port")
				exitWithUsage()
			}

			portStr := os.Args[i+1]
			i++
			serverPort, err = strconv.Atoi(portStr)
			if err != nil {
				fmt.Fprintln(os.Stderr, "Expected integer value for --port argument")
				exitWithUsage()
			}

		case "--num-threads":
			if i == len(os.Args)-1 {
				fmt.Fprintln(os.Stderr, "Expected argument after --num-threads")
				exitWithUsage()
			}
			numThreadsStr := os.Args[i+1]
			i++
			numThreads, err = strconv.Atoi(numThreadsStr)
			if err != nil {
				fmt.Fprintln(os.Stderr, "Expected positive integer value for --num-threads argument")
				exitWithUsage()
			}

		case "--help":
			fmt.Printf(usageMsg)
			os.Exit(0)

		default:
			fmt.Fprintf(os.Stderr, "Unexpected command line argument %s\n", os.Args[i])
			exitWithUsage()
		}
	}

	if requestHandler == nil {
		fmt.Fprintln(os.Stderr, "Must specify one of either --files or --proxy")
		exitWithUsage()
	}

	// Set up a handler for SIGINT, used in Task #4
	interrupted = make(chan os.Signal, 1)
	signal.Notify(interrupted, os.Interrupt)
	serveForever(numThreads, serverPort, requestHandler)
}
