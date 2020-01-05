class ButtonWatcher
  PIN_A = 37
  PIN_B = 39


  def run(timeout:, &block)
    start_watcher
    @running = true
    while @running
      event = wait_next_event(timeout)
      block.call(event) #use block.call instead of yield to work around https://github.com/mruby/mruby/issues/4921
    end
  ensure
    stop_watcher
  end

  def stop
    @running = false
  end


  class Event
    def inspect
      "#<Button::Event pin=#{pin} pressed=#{pressed} when=#{self.when} last_change=#{self.last_change}"
    end
  end
end
