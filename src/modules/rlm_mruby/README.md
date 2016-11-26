# rlm_mruby
## Metadata
<dl>
  <dt>category</dt><dd>languages</dd>
</dl>

## Summary
Allows the server to call a persistent, embedded mRuby script.

## TODO
There are a lot of possible improvements here:

* Restructure the code, the current status is mainly a PoC
* Allow `rlm_mruby` to be called from all sections instead of just `authorize`
* The methods have to be defined in the global namespace, define them on the module instead. I can't really find how to do this in mruby
  * Maybe better: define them as instance methods instead of module/class methods, which would rid us of the argument completely.
* Add other value lists than just request. We have the framework to do so
* Add support for passing a structure of the config, similar to `rlm_perl` and `rlm_python`. We can use the request object passed as a container to do so, so we don't have to define a global variable
* Add support for deleting values, `rlm_python` can serve as an example
* Add methods on the request object to modify the lists. The current return value is a bit ridiculous, a call like `request.config.add_vp('Cleartext-Password', 'hello')` looks a lot cleaner
* In a similar fashion: methods to get the attributes: `request.request.get_attribute('User-Name')`
* Add a xlat callback, similar to `rlm_perl` `radius_xlat`
