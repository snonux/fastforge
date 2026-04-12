var Keys = require('message_keys');

var CSV_STORAGE_KEY = 'fastforge.backup.csv';
var CSV_UPDATED_AT_KEY = 'fastforge.backup.csv.updated_at';

function storeCsv(rows) {
  var csv = rows.join('\n');
  localStorage.setItem(CSV_STORAGE_KEY, csv);
  localStorage.setItem(CSV_UPDATED_AT_KEY, new Date().toISOString());
  console.log('FastForge backup stored, bytes=' + csv.length);
}

function beginExportSession(total) {
  return {
    total: total,
    rows: new Array(total)
  };
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

  if (status === 'EXPORT_STARTED' || !onAppMessage.session || onAppMessage.session.total !== total || sequence === 0) {
    onAppMessage.session = beginExportSession(total);
  }

  var session = onAppMessage.session;
  if (!session || session.total !== total) {
    console.log('FastForge export session mismatch: ' + JSON.stringify(payload));
    return;
  }

  session.rows[sequence] = row;

  if (sequence + 1 === total) {
    storeCsv(session.rows);
    onAppMessage.session = null;
  }
}

Pebble.addEventListener('ready', function() {
  console.log('FastForge companion ready');
});

Pebble.addEventListener('appmessage', onAppMessage);
