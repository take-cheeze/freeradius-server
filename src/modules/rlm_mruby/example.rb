#This is example radius.rb script
def instantiate()
    Radiusd.radlog(Radiusd::L_DBG, "[mruby]Running ruby instantiate")
    puts "[mruby]Running mruby instantiate"
    return Radiusd::RLM_MODULE_OK
end
def authenticate(arg)
    Radiusd.radlog(Radiusd::L_DBG, "[mruby]Running ruby authenticate")
    return Radiusd::RLM_MODULE_NOOP
end
def authorize(arg)
    Radiusd.radlog(Radiusd::L_ERR, "[mruby]Running ruby authorize")
		Radiusd.radlog(Radiusd::L_WARN, "Authorize: #{arg.inspect}(#{arg.class})")
    Radiusd.radlog(Radiusd::L_WARN, "Authorize: #{arg.request.inspect}(#{arg.request.class})")
    #Here we return Cleartext-Password, which could have been retrieved from DB.
    return [Radiusd::RLM_MODULE_UPDATED, [],[["Cleartext-Password", "hello"]]]
end
def accounting(arg)
    Radiusd.radlog(Radiusd::L_DBG, "[mruby]Running ruby accounting")
    p arg
    return Radiusd::RLM_MODULE_NOOP
end
