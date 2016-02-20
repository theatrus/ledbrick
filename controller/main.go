package main

import (
	"github.com/theatrus/ledbrick/controller/ble"
	"github.com/theatrus/ledbrick/controller/ltable"
	"log"
	"flag"
	"io/ioutil"
)

var done = make(chan struct{})
var config = flag.String("config", "/etc/ledbrick-table.json", "Config file name")

func main() {
	flag.Parse()
	log.Println("LEDBrick Controller Master")
	log.Printf("Parsing config file %s", config)

	file, err := ioutil.ReadFile(*config)
	if err != nil {
		log.Printf("Error: %v", err)
		return
	}
	bleChannel := ble.NewBLEChannel()
	_, err = ltable.NewLightDriverFromJson(bleChannel, file)
	if err != nil {
		log.Printf("error in loading driver: %v", err)
		return
	}
	<-done
}
