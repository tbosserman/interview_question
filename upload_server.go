package main

import "os"
import "fmt"
import "log"
import "flag"
import "path/filepath"
import "net/http"
import "net/url"

const PARSE_QUERY = 0

func myHandler(writer http.ResponseWriter, reader *http.Request) {
    var urlValues       	url.Values
    var err	        	error
    var	key, fname, dir		string
    var values	        	[]string
    var i, length, flags        int
    var data	        	[]byte
    var fd			*os.File
    var stat			os.FileInfo

    // This stuff that parses the query is dead code but I don't want to
    // delete it so I can refer back to it some day.
    if PARSE_QUERY != 0 {
	if urlValues, err = url.ParseQuery(reader.URL.RawQuery); err != nil {
	    fmt.Fprintf(os.Stderr, "%s", err)
	    return
	}
	for key, values = range(urlValues) {
	    for i = 0; i < len(values); i += 1 {
		fmt.Printf("%-10.10s: %s\n", key, values[i])
	    }
	}

	if len(urlValues["fname"]) < 1 {
	    fmt.Println("No filename provided")
	} else {
	    fname = urlValues["fname"][0]
	    fmt.Printf("Filename='%s'\n", fname)
	}
    } else {
	// All the paths start with "/upload/" and we want to strip that
	// off on this side.
	fname = reader.URL.Path[8:]
    }

    if fname == "" {
        http.Error(writer, "no filename provided", 400)
        return
    }

    fmt.Println(fname)

    // Make sure the directory exists.
    dir = filepath.Dir(fname)
    stat, err = os.Stat(dir)
    if err != nil || !stat.IsDir() {
	if err = os.MkdirAll(dir, 0777); err != nil {
	    http.Error(writer, "mkdir failed", 500)
	    return
	}
    }

    flags = os.O_WRONLY | os.O_CREATE | os.O_TRUNC
    if fd, err = os.OpenFile(fname, flags, 0644); err != nil {
        http.Error(writer, err.Error(), 500)
        return
    }

    data = make([]byte, 8192)
    for length, err = reader.Body.Read(data); length > 0;
	length, err = reader.Body.Read(data) {
        fd.Write(data[:length])
    }
    fd.Close()
    if err != nil && err.Error() != "EOF" {
	fmt.Printf("ERROR: %s\n", err);
        http.Error(writer, err.Error(), 500)
    }

}

func main() {
    var port		*int
    var server		string

    port = flag.Int("p", 8080, "TCP port to listen on")
    flag.Parse()

    fmt.Printf("Listening on port %d\n", *port)
    server = fmt.Sprintf(":%d", *port)
    http.HandleFunc("/upload/", myHandler)
    log.Fatal(http.ListenAndServe(server, nil))
}
