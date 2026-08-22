#include "ncbind.hpp"
#include <set>

// static変数の実体

// auto register 先頭ポインタ
ncbAutoRegister::ThisClassT const*
ncbAutoRegister::_top[ncbAutoRegister::LINE_COUNT] = NCB_INNER_AUTOREGISTER_LINES_INSTANCE;

std::map<ttstr, ncbAutoRegister::INTERNAL_PLUGIN_LISTS > ncbAutoRegister::_internal_plugins;

// The reference static container initializer constructs TVPRegisteredPlugins
// first and this map second, registering their destructors in that order.
// Process exit therefore destroys the map first and the set second. Map/list
// teardown releases keys and index nodes only: stored registrar pointers are
// borrowed, and no Unregist callback runs from either container destructor.

bool ncbAutoRegister::LoadModule(const ttstr &_name)
{
	ttstr name = _name.AsLowerCase();
	// already load
    if (TVPRegisteredPlugins.find(name) != TVPRegisteredPlugins.end())
		return false;
	auto it = _internal_plugins.find(name);
	if (it != _internal_plugins.end()) {
		for (const auto & plugin_list : it->second.lists) {
            for (auto i : plugin_list) {
				i->Regist();
			}
		}
		TVPRegisteredPlugins.insert(name);
		return true;
	}
	return false;
}
