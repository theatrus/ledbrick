package ltable

import (
	"flag"
	"encoding/json"
	"time"
	"fmt"
	"log"
	"strings"
	"strconv"

	"github.com/theatrus/ledbrick/controller/ble"
)

var timeLocation *time.Location
var flagLocation string

func init() {
	// Setup a flag and provide a default location.
	flag.StringVar(&flagLocation, "ltable.location",
		"America/Los_Angeles", "The time zone to use for the location table")
}

func initLtables() {
	var err error
	timeLocation, err = time.LoadLocation(flagLocation)
	if err != nil {
		panic(fmt.Sprintf("Bad location in flag (%s): %v", flagLocation, err))
	}
}

type settingPoint struct {
	At string `json:"at"`
	Percents []float64 `json:"percents"`
}

func (sp settingPoint) TimeAt() time.Time {
	// Todo: make this more time parsey
	if timeLocation == nil {
		initLtables() // Lazy init
	}

	hm := strings.Split(sp.At, ":")
	hours, err := strconv.ParseInt(hm[0], 10, 32)
	if err != nil {
		log.Println("Bad hours, using 0: %s", err)
	}
	minutes, err := strconv.ParseInt(hm[1], 10, 32)
	if err != nil {
		log.Println("bad minutes, using 0: %s", err)
	}

	return time.Date(0, 0, 0, int(hours), int(minutes), 0, 0, timeLocation)
}

type settingPoints []settingPoint

func (s settingPoints) Len() int { return len(s) }
func (s settingPoints) Swap(i, j int) { s[i], s[j] = s[j], s[i] }
func (s settingPoints) Less(i, j int) bool {
	return s[i].TimeAt().Before(s[j].TimeAt())
}

func (ld settingPoints) percentForTime(t time.Time, channel int) float64 {
	if timeLocation == nil {
		initLtables() // Lazy init
	}

	// All the math is done in "local" time which may not be system
	// local time, so adjust everything to our location
	lt := t.In(timeLocation)
	// Grab a zero m/d/y version for comparison
	compareTime := time.Date(0, 0, 0, lt.Hour(), lt.Minute(), 0, 0, timeLocation)

	var iBefore int = len(ld) - 1 // Allow time to wrap
	var iAfter int = len(ld) - 1
	for i, v := range ld {
		if v.TimeAt().Equal(compareTime) {
			return v.Percents[channel]
		}

		if v.TimeAt().Before(compareTime) {
			iBefore = i
		} else {
			iAfter = i
			break
		}
	}

	valueBefore := ld[iBefore].Percents[channel]
	valueAfter := ld[iAfter].Percents[channel]

	// Don't interpolate
	if valueBefore == valueAfter {
		return valueAfter
	}

	difference := ld[iAfter].TimeAt().Sub(ld[iBefore].TimeAt()) / time.Second
	if difference < 0 {
		difference = difference * -1
	}
	nowDifference := compareTime.Sub(ld[iBefore].TimeAt()) / time.Second
	if nowDifference < 0 {
		nowDifference = nowDifference * -1
	}

	lerpMult := float64(nowDifference) / float64(difference)
	log.Printf("Lerp mult: %f, difference %f, now difference %f", lerpMult, difference, nowDifference)
	return valueBefore + lerpMult * (valueAfter - valueBefore)
}


type LightDriver struct {
	ble ble.BLEChannel
	settings settingPoints
	ticker *time.Ticker
}

func NewLightDriverFromJson(ble ble.BLEChannel, data []byte) (*LightDriver, error) {
	var settings settingPoints
	err := json.Unmarshal(data, settings)
	if err != nil {
		return nil, err
	}
	ld := &LightDriver{ble: ble,
		settings: settings,
		ticker: time.NewTicker(10 * time.Second),
	}

	go ld.run()
	return ld, nil
}

func (ld *LightDriver) run() {
	for _ = range ld.ticker.C {
		log.Println("Updating channel settings")
	}
}
