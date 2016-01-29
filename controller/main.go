package main

import (
	"github.com/theatrus/ledbrick/controller/ble"
	"log"
)

var done = make(chan struct{})

func main() {
	log.Println("LEDBrick Controller Master")

	_ = ble.NewBLEChannel()
	<-done
}
