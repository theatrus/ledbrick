package ble

import (
	"log"
	"sync"
	"time"
	"github.com/paypal/gatt"
)

const (
	pwmService = "000015231212efde1523785feabcd123"
	pwmLedChar = "000015251212efde1523785feabcd123"
	pwmTempChar = "000015261212efde1523785feabcd123"
	pwmFanChar = "000015241212efde1523785feabcd123"
)

type bleChannel struct {
	device gatt.Device
	connectedPeriph map[string]blePeriph
	knownPeriph map[string]bool
	ignoredPeriph map[string]bool
	connectingPeriph map[string]gatt.Peripheral
	idleTicker *time.Ticker

	lock sync.Mutex
}

type blePeriph struct {
	gp       gatt.Peripheral
	ledChar  *gatt.Characteristic
	fanChar  *gatt.Characteristic
	tempChar *gatt.Characteristic
}

type BLEChannel interface {

}

var DefaultClientOptions = []gatt.Option{
	gatt.LnxMaxConnections(10),
	gatt.LnxDeviceID(-1, true),
}

func NewBLEChannel() BLEChannel {
	d, err := gatt.NewDevice(DefaultClientOptions...)
	if err != nil {
		log.Fatalf("Failed to open the bluetooth HCI device: %s\n", err)
		return nil
	}

	ble := &bleChannel{device: d,
		connectedPeriph: make(map[string]blePeriph),
		knownPeriph: make(map[string]bool),
		ignoredPeriph: make(map[string]bool),
		connectingPeriph: make(map[string]gatt.Peripheral),
		idleTicker: time.NewTicker(100 * time.Millisecond),
	}

	d.Handle(
		gatt.PeripheralDiscovered(ble.onPeriphDiscovered),
		gatt.PeripheralConnected(ble.onPeriphConnected),
		gatt.PeripheralDisconnected(ble.onPeriphDisconnected),
	)


	d.Init(ble.onStateChanged)

	go func() {
		var i = 0.0

		for _ = range ble.idleTicker.C {
			for _, p := range ble.connectedPeriph {
				i += 0.1
				ble.lock.Lock()
				for c := 0; c < 8; c++ {
					err := p.gp.WriteCharacteristic(p.ledChar, []byte{byte(c), //0x00}, true)
					byte(i * float64(c)) % 0x80 + 0x20}, true)
					if err != nil {
						log.Println(err)
					}

				}
				ble.lock.Unlock()
			}
		}
	}()

	return &bleChannel{device: d}
}

// Force Gatt to enter scanning mode
func (ble *bleChannel) onStateChanged(d gatt.Device, s gatt.State) {
	log.Println("State:", s)
	switch s {
	case gatt.StatePoweredOn:
		log.Println("Scanning...")
		d.Scan([]gatt.UUID{}, true)
		return
	default:
		log.Println("Stop scanning")
		d.StopScanning()
	}
}

func (ble *bleChannel) onPeriphConnected(p gatt.Peripheral, err error) {
	ble.lock.Lock()
	defer ble.lock.Unlock()

	log.Println("Connected ", p.ID())
	// Remove from the connecting pool
	delete(ble.connectingPeriph, p.ID())

	bp := blePeriph{gp: p}


	// Discovery services
	ss, err := p.DiscoverServices(nil)
	if err != nil {
		log.Printf("Failed to discover services, err: %s\n", err)
		return
	}

	for _, s := range ss {
		msg := "Service: " + s.UUID().String()
		if len(s.Name()) > 0 {
			msg += " (" + s.Name() + ")"
		}
		log.Println(msg)

		// Discovery characteristics
		cs, err := p.DiscoverCharacteristics(nil, s)
		if err != nil {
			log.Printf("Failed to discover characteristics, err: %s\n", err)
			continue
		}

		for _, c := range cs {
			msg := "  Characteristic  " + c.UUID().String()

			// Grab and store the three characteristics we
			// case about by matching by UUID
			switch c.UUID().String() {
			case pwmLedChar:
				bp.ledChar = c
			case pwmTempChar:
				bp.tempChar = c
			case pwmFanChar:
				bp.fanChar = c
			}

			if len(c.Name()) > 0 {
				msg += " (" + c.Name() + ")"
			}
			msg += "\n    properties    " + c.Properties().String()
			log.Println(msg)

			// Read the characteristic, if possible.
			if (c.Properties() & gatt.CharRead) != 0 {
				b, err := p.ReadCharacteristic(c)
				if err != nil {
					log.Printf("Failed to read characteristic, err: %s\n", err)
					continue
				}
				log.Printf("    value         %x | %q\n", b, b)
			}

			// Discovery descriptors
			ds, err := p.DiscoverDescriptors(nil, c)
			if err != nil {
				log.Printf("Failed to discover descriptors, err: %s\n", err)
				continue
			}

			for _, d := range ds {
				msg := "  Descriptor      " + d.UUID().String()
				if len(d.Name()) > 0 {
					msg += " (" + d.Name() + ")"
				}
				log.Println(msg)

				// Read descriptor (could fail, if it's not readable)
				b, err := p.ReadDescriptor(d)
				if err != nil {
					log.Printf("Failed to read descriptor, err: %s\n", err)
					continue
				}
				log.Printf("    value         %x | %q\n", b, b)
			}

			// Subscribe the characteristic, if possible.
			if (c.Properties() & (gatt.CharNotify | gatt.CharIndicate)) != 0 {
				f := func(c *gatt.Characteristic, b []byte, err error) {
					log.Printf("notified: % X | %q\n", b, b)
				}
				if err := p.SetNotifyValue(c, f); err != nil {
					log.Printf("Failed to subscribe characteristic, err: %s\n", err)
					continue
				}
			}

		}
	}

	ble.connectedPeriph[p.ID()] = bp
}


func (ble *bleChannel) onPeriphDiscovered(p gatt.Peripheral, a *gatt.Advertisement, rssi int) {
	ble.lock.Lock()
	defer ble.lock.Unlock()


	if _, ok := ble.ignoredPeriph[p.ID()]; ok {
		return
	}

	log.Printf("Peripheral ID:%s, NAME:(%s)\n", p.ID(), p.Name())
	log.Println("  Local Name        =", a.LocalName)
	log.Println("  TX Power Level    =", a.TxPowerLevel)
	log.Println("  Manufacturer Data =", a.ManufacturerData)
	log.Println("  Service Data      =", a.ServiceData)
	log.Println("")

	if p.Name() != "LEDBrick-PWM" {
		ble.ignoredPeriph[p.ID()] = true
		log.Println("Ignoring this device.")
		return
	}

	ble.knownPeriph[p.ID()] = true
	if _, ok := ble.connectingPeriph[p.ID()]; ok {
		log.Printf("Peripheral is in connecting state: %s", p.ID())
		return
	}
	log.Printf("Connecting to %s", p.ID())
	ble.connectingPeriph[p.ID()] = p
	go func() {
		time.Sleep(30 * time.Second)
		if _, ok := ble.connectedPeriph[p.ID()]; ok {
			return
		} else {
			delete(ble.connectingPeriph, p.ID())
			log.Printf("Haven't heard back about connection to  %s", p.ID())
		}
	}()
	p.Device().Connect(p)
}

func (ble *bleChannel) onPeriphDisconnected(p gatt.Peripheral, err error) {
	ble.lock.Lock()
	defer ble.lock.Unlock()

	log.Println("Disconnected ", p.ID())
	delete(ble.connectedPeriph, p.ID())
	p.Device().CancelConnection(p)
}
