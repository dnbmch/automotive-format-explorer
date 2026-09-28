pragma Singleton
import QtQuick

// Stands in for the application's controller: the nav panel reads only its
// sample list and asks it to open a sample.
QtObject {
    property var sampleFiles: []
    function openFile(url) {}
}
