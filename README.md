# mruby-m5stack-button-watcher

A very simple button class for m5stack / m5stickc

# Usage

initialize a watcher by passing the array of pins to watch

```
watcher = ButtonWatcher.new([ButtonWatcher::PIN_A, ButtonWatcher::PIN_B])
```

then call `run`. this method will yield events to the block whenever they occur or when the timeout (in ms) has expired:

```
watcher.run(timeout: 5000) do |event|
  if event
    puts "#{event.pin} state=#{event.pressed}"
  else
    puts "no event occurred"
  end
end
```

event also has these propertires:

`when`: when the current event was dispatched
`last_change`: when the previous event on that pin occured
