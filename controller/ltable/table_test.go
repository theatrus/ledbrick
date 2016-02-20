package ltable

import (
	"testing"
	"sort"
	"time"
)

var percents = []float64{1.0, 2.0}
var percents1 = []float64{0, 0.0}
var percents2 = []float64{100, 50.0}

func TestTimeAt(t *testing.T) {
	sp := settingPoint{At: "10:12", Percents: percents}
	ta := sp.TimeAt()
	if ta.Hour() != 10 || ta.Minute() != 12 {
		t.Error("Wrong time")
	}
}

func TestSortTimes(t *testing.T) {
	sps := settingPoints(
		[]settingPoint{
			settingPoint{At: "10:11", Percents: percents},
			settingPoint{At: "9:23", Percents: percents},
	})
	sort.Sort(sps)
	if sps[0].At != "9:23" {
		t.Error("Sorting failed")
	}
}

func TestPercentForTime(t *testing.T) {
	initLtables()

	sps := settingPoints(
		[]settingPoint{
			settingPoint{At: "10:00", Percents: percents1},
			settingPoint{At: "11:00", Percents: percents2},
			settingPoint{At: "12:00", Percents: percents1},
	})
	sort.Sort(sps)

	now := time.Date(2016, 1, 1, 10, 0, 0, 0, timeLocation)
	value := sps.percentForTime(now, 0)
	if value != percents1[0] {
		t.Errorf("Value was not %f, got %f", percents1[0], value)
	}

	now = time.Date(2016, 1, 1, 10, 30, 0, 0, timeLocation)
	value = sps.percentForTime(now, 0)
	if value != 50 {
		t.Errorf("Value was not 50, got %f", value)
	}

	now = time.Date(2016, 1, 1, 11, 15, 0, 0, timeLocation)
	value = sps.percentForTime(now, 0)
	if value != 75 {
		t.Errorf("Value was not 75, got %f", value)
	}

	// Check for negative wrap around
	now = time.Date(2016, 1, 1, 9, 30, 0, 0, timeLocation)
	value = sps.percentForTime(now, 0)
	if value != 0 {
		t.Errorf("Value was not 0, got %f", value)
	}

	// Check for positive wrap around
	now = time.Date(2016, 1, 1, 13, 30, 0, 0, timeLocation)
	value = sps.percentForTime(now, 0)
	if value != 0 {
		t.Errorf("Value was not 0, got %f", value)
	}
}
