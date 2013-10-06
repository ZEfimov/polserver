/*
History
=======


Notes
=======

*/

#ifndef __OBJECTHASH_H
#define __OBJECTHASH_H

#include "reftypes.h"
#include "../clib/rawtypes.h"

/*
template <class _Key> struct myhash { };
_STLP_TEMPLATE_NULL struct myhash<UObject*> {
  size_t operator()(UObject* obj) const { return obj->serial; }
};
*/
class ObjectHash
{
public:
# if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 3 )
	typedef unordered_set<u32> ds;
	//typedef unordered_map<u32,UObjectRef> hs;
# else
	typedef unordered_set<u32> ds;
	//typedef hash_map<u32,UObjectRef> hs;
# endif
	typedef pair<u32,UObjectRef> hashpair;
	typedef map<u32,UObjectRef> hs;
	typedef hs::iterator OH_iterator;
	typedef hs::const_iterator OH_const_iterator;

	ObjectHash();
	~ObjectHash();

	bool Insert(UObject* obj);
	bool Remove(u32 serial);
    void Clear();
    void Reap();

	UObject* Find(u32 serial);
	u32 GetNextUnusedItemSerial();
	u32 GetNextUnusedCharSerial();
	void PrintContents( StreamWriter& sw ) const;

    hs::const_iterator begin() const;
    hs::const_iterator end() const;
    void ClearCharacterAccountReferences();

    ds::const_iterator dirty_deleted_begin() const;
    ds::const_iterator dirty_deleted_end() const;

    void CleanDeleted();
    void ClearDeleted();

    void RegisterCleanDeletedSerial( u32 serial );

private:
	hs hash;
    OH_iterator reap_iterator;

    ds dirty_deleted;
    ds clean_deleted;
};

extern ObjectHash objecthash;
#endif
