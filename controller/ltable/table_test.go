package ltable

import (
	"testing"
	"sort"
)

var percents = []float64{1.0, 2.0}

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
