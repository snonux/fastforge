var Keys = require('message_keys');

var CSV_STORAGE_KEY = 'fastforge.backup.csv';
var CSV_UPDATED_AT_KEY = 'fastforge.backup.csv.updated_at';

function ensureArraySize(rows, total) {
  if (!rows || rows.length !== total) {
    return new Array(total);
  }
  return rows;
}

function storeCsv(rows) {
  var csv = rows.join('\n');
  localStorage.setItem(CSV_STORAGE_KEY, csv);
  localStorage.setItem(CSV_UPDATED_AT_KEY, new Date().toISOString());
  console.log('FastForge backup stored, bytes=' + csv.length);
}

function requestExport() {
  var message = {};
  message[Keys.EXPORT_COMMAND] = 'EXPORT_HISTORY';
  Pebble.sendAppMessage(message, function() {
    console.log('FastForge export request sent');
  }, function(error) {
    console.log('FastForge export request failed: ' + JSON.stringify(error));
  });
}

function onAppMessage(event) {
  var payload = event.payload || {};
  var status = payload[Keys.EXPORT_STATUS];
  var sequence = payload[Keys.EXPORT_SEQUENCE];
  var total = payload[Keys.EXPORT_TOTAL];
  var row = payload[Keys.EXPORT_ROW];

  if (typeof status === 'string') {
    console.log('FastForge export status: ' + status);
  }

  if (typeof sequence !== 'number' || typeof total !== 'number' || typeof row !== 'string') {
    if (typeof status !== 'string') {
      console.log('FastForge backup chunk missing fields: ' + JSON.stringify(payload));
    }
    return;
  }

  var rows = ensureArraySize(onAppMessage.rows, total);
  rows[sequence] = row;
  onAppMessage.rows = rows;

  if (sequence + 1 === total) {
    storeCsv(rows);
    onAppMessage.rows = null;
  }
}

Pebble.addEventListener('ready', function() {
  console.log('FastForge companion ready');
  requestExport();
});

Pebble.addEventListener('appmessage', onAppMessage);
