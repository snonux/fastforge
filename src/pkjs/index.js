/* FastForge companion stub.
 *
 * FastForge is entirely on-watch: history lives in Pebble persistent storage
 * and nothing is sent to the phone. This file exists only because the build
 * (see wscript, js_entry_file) always bundles a PebbleKit JS entry point. */

Pebble.addEventListener('ready', function() {
  console.log('FastForge companion ready');
});
