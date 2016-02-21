package ble

import (
	"errors"
	"github.com/paypal/gatt"
	"log"
	"sync"
	"time"
)

const (
	pwmService  = "000015231212efde1523785feabcd123"
	pwmLedChar  = "000015251212efde1523785feabcd123"
	pwmTempChar = "000015261212efde1523785feabcd123"
	pwmFanChar  = "000015241212efde1523785feabcd123"
)

var DefaultClientOptions = []gatt.Option{
	gatt.LnxMaxConnections(10),
	gatt.LnxDeviceID(-1, true),
}

type bleChannel struct {
	device           gatt.Device
	connectedPeriph  map[string]*blePeriph
	knownPeriph      map[string]bool
	ignoredPeriph    map[string]bool
	connectingPeriph map[string]gatt.Peripheral
	idleTicker       *time.Ticker

	channelSetting map[int]float64

	lock sync.Mutex
}

type blePeriph struct {
	active   bool
	gp       gatt.Peripheral
	ledChar  *gatt.Characteristic
	fanChar  *gatt.Characteristic
	tempChar *gatt.Characteristic

	temperature int
	fanRpm      int
}

type BLEPeripheral interface {
	Active() bool
	Temperature() int
	FanRPM() int
}

func (p *blePeriph) Active() bool     { return p.active }
func (p *blePeriph) Temperature() int { return p.temperature }
func (p *blePeriph) FanRPM() int      { return p.fanRpm }

type BLEChannel interface {
	Perhipherals() []BLEPeripheral
	SetChannel(channel int, percent float64) error
}

func NewBLEChannel() BLEChannel {
	d, err := gatt.NewDevice(DefaultClientOptions...)
	if err != nil {
		log.Fatalf("Failed to open the bluetooth HCI device: %s\n", err)
		return nil
	}

	ble := &bleChannel{device: d,
		connectedPeriph:  make(map[string]*blePeriph),
		knownPeriph:      make(map[string]bool),
		ignoredPeriph:    make(map[string]bool),
		connectingPeriph: make(map[string]gatt.Peripheral),
		idleTicker:       time.NewTicker(1500 * time.Millisecond),
		channelSetting:   make(map[int]float64),
	}

	d.Handle(
		gatt.PeripheralDiscovered(ble.onPeriphDiscovered),
		gatt.PeripheralConnected(ble.onPeriphConnected),
		gatt.PeripheralDisconnected(ble.onPeriphDisconnected),
	)

	d.Init(ble.onStateChanged)

	// Green CYan PCAmber Blue Red DeepBlue White UV
	// Percents
	initPower := []int{10, 30, 10, 40, 10, 40, 30, 40}
	for i, v := range initPower {
		ble.channelSetting[i] = float64(v)
	}

	go func() {
		for _ = range ble.idleTicker.C {
			_ = ble.writeLedState()
		}
	}()

	return ble
}

func (ble *bleChannel) writeLedState() error {

	ble.lock.Lock()
	defer ble.lock.Unlock()

	for _, p := range ble.connectedPeriph {
		for channel := 0; channel <= 7; channel++ {
			// Max intensity limit is about 0xfa
			value := int((ble.channelSetting[channel] / 100.0) * 0xfa)
			err := p.gp.WriteCharacteristic(p.ledChar,
				[]byte{byte(channel), byte(value)}, true)
			if err != nil {
				log.Println("Command send error: %s", err)
			}
		}

	}
	return nil
}

func (ble *bleChannel) Perhipherals() []BLEPeripheral {
	p := make([]BLEPeripheral, 0)
	for _, periph := range ble.connectedPeriph {
		p = append(p, periph)
	}
	return p
}

func (ble *bleChannel) SetChannel(channel int, percent float64) error {
	if percent < 0 || percent > 100 {
		return errors.New("Out of range percent (0-100)")
	}
	ble.channelSetting[channel] = percent
	return ble.writeLedState()
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

	log.Println("Connected, starting interrogation of ", p.ID())
	bp := blePeriph{gp: p,
		active: true}

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
			return
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
					return
				}
				log.Printf("    value         %x | %q\n", b, b)
			}

			// Discovery descriptors
			ds, err := p.DiscoverDescriptors(nil, c)
			if err != nil {
				log.Printf("Failed to discover descriptors, err: %s\n", err)
				return
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
					return
				}
				log.Printf("    value         %x | %q\n", b, b)
			}

			// Subscribe the characteristic, if possible.
			if (c.Properties() & (gatt.CharNotify | gatt.CharIndicate)) != 0 {
				f := func(c *gatt.Characteristic, b []byte, err error) {
					//log.Printf("%s: % X | %q\n", p.ID(), b, b)
					switch c.UUID().String() {
					case pwmTempChar:
						bp.temperature = int(b[0])
						log.Printf("%s: temperature: %d C", p.ID(), bp.temperature)
					case pwmFanChar:
						bp.fanRpm = int(b[0]) | (int(b[1]) << 8)
						log.Printf("%s: fan speed: %d rpm", p.ID(), bp.fanRpm)
					default:
						log.Printf("unknown notification from %s", p.ID())
					}
				}
				if err := p.SetNotifyValue(c, f); err != nil {
					log.Printf("Failed to subscribe characteristic, err: %s\n", err)
					return
				}
			}

		}
	}

	ble.lock.Lock()
	defer ble.lock.Unlock()

	// Remove from the connecting pool
	delete(ble.connectingPeriph, p.ID())

	ble.connectedPeriph[p.ID()] = &bp
	log.Printf("Peripheral connection complete: %s", p.ID())
}

func (ble *bleChannel) onPeriphDiscovered(p gatt.Peripheral, a *gatt.Advertisement, rssi int) {
	ble.lock.Lock()
	defer ble.lock.Unlock()

	if _, ok := ble.ignoredPeriph[p.ID()]; ok {
		return
	}

	ble.knownPeriph[p.ID()] = true
	if _, ok := ble.connectingPeriph[p.ID()]; ok {
		log.Printf("Peripheral is in connecting state: %s", p.ID())
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

	log.Printf("Connecting to %s", p.ID())
	ble.connectingPeriph[p.ID()] = p
	go func() {
		time.Sleep(30 * time.Second)
		if _, ok := ble.connectedPeriph[p.ID()]; ok {
			return
		} else {
			delete(ble.connectingPeriph, p.ID())
			log.Printf("Haven't heard back about connection to %s, removing from pending pool", p.ID())
		}
	}()
	p.Device().Connect(p)
}

func (ble *bleChannel) onPeriphDisconnected(p gatt.Peripheral, err error) {
	ble.lock.Lock()
	defer ble.lock.Unlock()

	log.Println("Disconnected ", p.ID())

	localPeriph := ble.connectedPeriph[p.ID()]
	// If the API has given an active handle to this peripheral out,
	// we need to be able to flag it as no longer active. A simple
	// boolean suffices.
	if localPeriph != nil {
		localPeriph.active = false
	}

	delete(ble.connectedPeriph, p.ID())
	// We re-cancel the connection here, which will free any associated
	// channels if this disconnect is due to the peripheral initiating the disconnect
	p.Device().CancelConnection(p)
}
