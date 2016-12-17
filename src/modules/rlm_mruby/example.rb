#This is example radius.rb script
module Radiusd
    def self.instantiate()
        radlog(L_DBG, "[mruby]Running ruby instantiate")
        return RLM_MODULE_OK
    end
    def self.authenticate(arg)
        radlog(L_DBG, "[mruby]Running ruby authenticate")
        return RLM_MODULE_NOOP
    end
    def self.authorize(arg)
        radlog(L_ERR, "[mruby]Running ruby authorize")
        radlog(L_WARN, "Authorize: #{arg.inspect}(#{arg.class})")
        radlog(L_WARN, "Authorize: #{arg.request.inspect}(#{arg.request.class})")
    
        reply = [["Framed-MTU", 1500]]
        control = [["Cleartext-Password", "hello"], ["Tmp-String-0", "!*", "ANY"]]
        return [RLM_MODULE_UPDATED, reply, control]
    end
    def self.post_auth(arg)
        radlog(L_DBG, "[mruby]Running ruby post_auth")
        return RLM_MODULE_NOOP
    end
    def self.accounting(arg)
        radlog(L_DBG, "[mruby]Running ruby accounting")
        return RLM_MODULE_NOOP
    end
end
