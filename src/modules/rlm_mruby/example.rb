#This is example radius.rb script
def instantiate()
    Radiusd.radlog(Radiusd::L_DBG, "[mruby]Running ruby instantiate")
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

    reply = [["Framed-MTU", 1500]]
    control = [["Cleartext-Password", "hello"], ["Tmp-String-0", "!*", "ANY"]]
    return [Radiusd::RLM_MODULE_UPDATED, reply, control]
end
def accounting(arg)
    Radiusd.radlog(Radiusd::L_DBG, "[mruby]Running ruby accounting")
    return Radiusd::RLM_MODULE_NOOP
end
