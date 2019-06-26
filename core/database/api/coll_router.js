/*jshint strict: global */
/*jshint esversion: 6 */
/*jshint multistr: true */
/* globals require */
/* globals module */
/* globals console */

'use strict';

const   createRouter = require('@arangodb/foxx/router');
const   router = createRouter();
const   joi = require('joi');

const   g_db = require('@arangodb').db;
const   g_graph = require('@arangodb/general-graph')._graph('sdmsg');
const   g_lib = require('./support');

module.exports = router;


//===== COLLECTION API FUNCTIONS =====

router.post('/create', function (req, res) {
    try {
        var result = [];

        g_db._executeTransaction({
            collections: {
                read: ["u","uuid","accn"],
                write: ["c","a","alias","owner","item"]
            },
            action: function() {
                const client = g_lib.getUserFromClientID( req.queryParams.client );
                var owner = client,parent_id;

                if ( req.body.parent ) {
                    parent_id = g_lib.resolveCollID( req.body.parent, client );

                    var owner_id = g_db.owner.firstExample({_from:parent_id})._to;
                    if ( owner_id != client._id ){
                        if ( !g_lib.hasManagerPermProj( client, owner_id )){
                            var parent_coll = g_db.c.document( parent_id );

                            if ( !g_lib.hasPermissions( client, parent_coll, g_lib.PERM_CREATE ))
                                throw g_lib.ERR_PERM_DENIED;
                        }
                        owner = g_db._document( owner_id );
                    }
                }else{
                    parent_id = g_lib.getRootID(client._id);
                }

                // Must have at least one allocation to create collections
                if ( !g_db.alloc.firstExample({_from: owner._id }) ){
                    if ( owner._id[0] != 'p' || !owner.sub_repo )
                        throw [g_lib.ERR_NO_ALLOCATION,"Allocation required to create collections"];
                }

                // Enforce collection limit if set
                if ( owner.max_coll >= 0 ){
                    var count = g_db._query("return length(FOR i IN owner FILTER i._to == @id and is_same_collection('c',i._from) RETURN 1)",{id:owner._id}).next();
                    if ( count >= owner.max_coll )
                        throw [g_lib.ERR_ALLOCATION_EXCEEDED,"Collection limit reached ("+client.max_coll+"). Contact system administrator to increase limit."];
                }

                var time = Math.floor( Date.now()/1000 );
                var obj = { owner: owner._id, ct: time, ut: time };

                g_lib.procInputParam( req.body, "title", false, obj );
                g_lib.procInputParam( req.body, "desc", false, obj );
                g_lib.procInputParam( req.body, "alias", false, obj );

                var coll = g_db.c.save( obj, { returnNew: true });
                g_db.owner.save({ _from: coll._id, _to: owner._id });

                g_graph.item.save({ _from: parent_id, _to: coll._id });

                if ( obj.alias ) {
                    var alias_key = owner._id[0] + ":" + owner._id.substr(2) + ":" + obj.alias;

                    g_db.a.save({ _key: alias_key });
                    g_db.alias.save({ _from: coll._id, _to: "a/" + alias_key });
                    g_db.owner.save({ _from: "a/" + alias_key, _to: owner._id });
                }

                coll = coll.new;
                coll.id = coll._id;
                coll.parent_id = parent_id;
                delete coll._id;
                delete coll._key;
                delete coll._rev;

                result.push( coll );
            }
        });

        res.send( result );
    } catch( e ) {
        g_lib.handleException( e, res );
    }
})
.queryParam('client', joi.string().required(), "Client ID")
.body(joi.object({
    title: joi.string().allow('').optional(),
    desc: joi.string().allow('').optional(),
    alias: joi.string().allow('').optional(),
    public: joi.boolean().optional(),
    parent: joi.string().allow('').optional()
}).required(), 'Collection fields')
.summary('Create a new data collection')
.description('Create a new data collection from JSON body');

router.post('/update', function (req, res) {
    try {
        var result = [];

        g_db._executeTransaction({
            collections: {
                read: ["u","uuid","accn"],
                write: ["c","a","owner","alias"]
            },
            action: function() {
                const client = g_lib.getUserFromClientID( req.queryParams.client );
                var coll_id = g_lib.resolveCollID( req.body.id, client );
                var coll = g_db.c.document( coll_id );

                if ( !g_lib.hasAdminPermObject( client, coll_id )) {
                    if ( !g_lib.hasPermissions( client, coll, g_lib.PERM_WR_REC ))
                        throw g_lib.ERR_PERM_DENIED;
                }

                var time = Math.floor( Date.now()/1000 );
                var obj = {ut:time};

                g_lib.procInputParam( req.body, "title", true, obj );
                g_lib.procInputParam( req.body, "desc", true, obj );
                g_lib.procInputParam( req.body, "alias", true, obj );

                coll = g_db._update( coll_id, obj, { keepNull: false, returnNew: true });
                coll = coll.new;

                if ( obj.alias != undefined ) {
                    var old_alias = g_db.alias.firstExample({ _from: coll_id });
                    if ( old_alias ) {
                        const graph = require('@arangodb/general-graph')._graph('sdmsg');
                        graph.a.remove( old_alias._to );
                    }

                    if ( obj.alias ){
                        var owner_id = g_db.owner.firstExample({ _from: coll_id })._to;
                        var alias_key = owner_id[0] + ":" + owner_id.substr(2) + ":" + obj.alias;

                        g_db.a.save({ _key: alias_key });
                        g_db.alias.save({ _from: coll_id, _to: "a/" + alias_key });
                        g_db.owner.save({ _from: "a/" + alias_key, _to: owner_id });
                    }
                }

                if ( obj.public != undefined ){
                    // Public flag has changed - process all contained items recursively
                }

                delete coll._rev;
                delete coll._key;
                coll.id = coll._id;
                delete coll._id;

                result.push( coll );
            }
        });

        res.send( result );
    } catch( e ) {
        g_lib.handleException( e, res );
    }
})
.queryParam('client', joi.string().required(), "Client ID")
.body(joi.object({
    id: joi.string().required(),
    title: joi.string().allow('').optional(),
    desc: joi.string().allow('').optional(),
    alias: joi.string().allow('').optional(),
    public: joi.boolean().optional()
}).required(), 'Collection fields')
.summary('Update an existing collection')
.description('Update an existing collection from JSON body');

router.get('/delete', function (req, res) {
    try {
        g_db._executeTransaction({
            collections: {
                read: ["u","uuid","accn"],
                write: ["c","d","a","owner","item","loc","acl","alias","alloc","t","top","p","dep"]
            },
            action: function() {
                const client = g_lib.getUserFromClientID( req.queryParams.client );
                var coll_id = g_lib.resolveCollID( req.queryParams.id, client );
                var coll = g_db.c.document( coll_id );

                if ( !g_lib.hasAdminPermObject( client, coll_id )) {
                    if ( !g_lib.hasPermissions( client, coll, g_lib.PERM_DELETE ))
                        throw g_lib.ERR_PERM_DENIED;
                }

                if ( coll.is_root )
                    throw [g_lib.ERR_INVALID_PARAM,"Cannot delete root collection"];

                var locations={}, alloc_sz={};
                g_lib.deleteCollection( coll._id, alloc_sz, locations );
                g_lib.updateAllocations( alloc_sz );

                res.send( locations );
            }
        });
    } catch( e ) {
        g_lib.handleException( e, res );
    }
})
.queryParam('client', joi.string().required(), "Client ID")
.queryParam('id', joi.string().required(), "Collection ID or alias")
.summary('Deletes an existing data collection')
.description('Deletes an existing data collection and contained data');

// This is an OWNER or ADMIN only function, other users must navigate the collection hierarchy
router.get('/priv/list', function (req, res) {
    try {
        const client = g_lib.getUserFromClientID( req.queryParams.client );
        var owner_id;

        if ( req.queryParams.subject ) {
            owner_id = req.queryParams.subject;
        } else {
            owner_id = client._id;
        }

        if ( client.is_admin || owner_id == client._id || g_lib.db.admin.firstExample({ _from: owner_id, _to: client._id }) ) {
            var result = g_db._query( "for v in 1..1 inbound @owner owner filter IS_SAME_COLLECTION('c', v) return { id: v._id, title: v.title }", { owner: owner_id }).toArray();
            res.send( result );
        } else
            throw g_lib.ERR_PERM_DENIED;
    } catch( e ) {
        g_lib.handleException( e, res );
    }
})
.queryParam('client', joi.string().required(), "Client ID")
.queryParam('subject', joi.string().optional(), "UID of subject user (optional)")
.summary('List all data collections owned by client (or subject)')
.description('List all data collections owned by client (or subject)');


router.get('/view', function (req, res) {
    try {
        const client = g_lib.getUserFromClientID( req.queryParams.client );

        var coll_id = g_lib.resolveCollID( req.queryParams.id, client );
        var coll = g_db.c.document( coll_id );

        if ( !g_lib.hasAdminPermObject( client, coll_id )) {
            if ( !g_lib.hasPermissions( client, coll, g_lib.PERM_RD_REC ))
                throw g_lib.ERR_PERM_DENIED;
        }

        coll.id = coll._id;

        delete coll._id;
        delete coll._key;
        delete coll._rev;

        res.send( [coll] );
    } catch( e ) {
        g_lib.handleException( e, res );
    }
})
.queryParam('client', joi.string().required(), "Client ID")
.queryParam('id', joi.string().required(), "Collection ID or alias")
.summary('View collection information by ID or alias')
.description('View collection information by ID or alias');


router.get('/read', function (req, res) {
    try {
        const client = g_lib.getUserFromClientID( req.queryParams.client );
        var coll_id = g_lib.resolveCollID( req.queryParams.id, client );
        var coll = g_db.c.document( coll_id );

        if ( !g_lib.hasAdminPermObject( client, coll_id )) {
            if ( !g_lib.hasPermissions( client, coll, g_lib.PERM_LIST ))
                throw g_lib.ERR_PERM_DENIED;
        }

        var qry = "for v in 1..1 outbound @coll item sort is_same_collection('c',v) DESC, v.title";
        var result;

        if ( req.queryParams.offset != undefined && req.queryParams.count != undefined ){
            qry += " limit " + req.queryParams.offset + ", " + req.queryParams.count;
            qry += " return { id: v._id, title: v.title, alias: v.alias, locked: v.locked }";
            result = g_db._query( qry, { coll: coll_id },{},{fullCount:true});
            var tot = result.getExtra().stats.fullCount;
            result = result.toArray();
            result.push({paging:{off:req.queryParams.offset,cnt:req.queryParams.count,tot:tot}});
        }
        else{
            qry += " return { id: v._id, title: v.title, alias: v.alias, locked: v.locked }";
            result = g_db._query( qry, { coll: coll_id });
        }

        res.send( result );
    } catch( e ) {
        g_lib.handleException( e, res );
    }
})
.queryParam('client', joi.string().required(), "Client ID")
.queryParam('id', joi.string().required(), "Collection ID or alias to list")
.queryParam('offset', joi.number().optional(), "Offset")
.queryParam('count', joi.number().optional(), "Count")
.summary('Read contents of a collection by ID or alias')
.description('Read contents of a collection by ID or alias');


router.get('/write', function (req, res) {
    try {
        g_db._executeTransaction({
            collections: {
                read: ["u","d","c","uuid","accn"],
                write: ["item"]
            },
            action: function() {
                const client = g_lib.getUserFromClientID( req.queryParams.client );

                var coll_id = g_lib.resolveCollID( req.queryParams.id, client );
                var coll = g_db.c.document( coll_id );
                var owner_id = g_db.owner.firstExample({ _from: coll_id })._to;

                if ( !g_lib.hasAdminPermObject( client, coll_id )) {
                    var req_perm = g_lib.PERM_LINK;
                    if ( req.queryParams.remove && req.queryParams.remove.length )
                        req_perm |= g_lib.PERM_SHARE;
                    if ( !g_lib.hasPermissions( client, coll, req_perm ))
                        throw g_lib.ERR_PERM_DENIED;
                }

                var i, obj,idx;
                var loose = [];

                // Enforce following link/unlink rules:
                // 1. Root collection may not be linked
                // 2. Items can only be linked once to a given collection
                // 3. Only items sharing the same owner as the target collection may be linked
                // 4. Linking and unlinking requires WRITE permission on parent collections and ADMIN permission on item
                // 5. Circular links are not allowed (linking a parent into a child collection)
                // 6. Collections can only be linked to one parent
                // 7. All records and collections must have at least one parent (except root)

                if ( req.queryParams.remove ) {
                    for ( i in req.queryParams.remove ) {
                        obj = g_lib.getObject( req.queryParams.remove[i], client );

                        if ( !g_db.item.firstExample({ _from: coll_id, _to: obj._id }))
                            throw [g_lib.ERR_UNLINK,obj._id+" is not in collection " + coll_id];

                        g_db.item.removeByExample({ _from: coll_id, _to: obj._id });

                        if ( !g_db.item.firstExample({ _to: obj._id }) ){
                            loose.push({ id: obj._id, title: obj.title });
                        }
                    }
                }

                if ( req.queryParams.add ) {
                    for ( i in req.queryParams.add ) {
                        obj = g_lib.getObject( req.queryParams.add[i], client );

                        // Check if item is already in this collection
                        if ( g_db.item.firstExample({ _from: coll_id, _to: obj._id }))
                            throw [g_lib.ERR_LINK,obj._id+" already linked to "+coll_id];

                        // Check if item is a root collection
                        if ( obj.is_root )
                            throw [g_lib.ERR_LINK,"Cannot link root collection"];

                        // Check if item has same owner as this collection
                        if ( g_db.owner.firstExample({ _from: obj._id })._to != owner_id )
                            throw [g_lib.ERR_LINK,obj._id+" and "+coll_id+" have different owners"];

                        if ( obj._id[0] == "c" ){
                            // Check for circular dependency
                            if ( obj._id == coll_id || g_lib.isSrcParentOfDest( obj._id, coll_id ))
                                throw [g_lib.ERR_LINK,"Cannot link ancestor, "+obj._id+", to descendant, "+coll_id];

                            // Collections can only be linked to one parent
                            g_db.item.removeByExample({ _to: obj._id });
                        }

                        g_db.item.save({ _from: coll_id, _to: obj._id });

                        // If item has no parent collection AND it's not being added, link to root
                        for ( idx in loose ){
                            if ( loose[idx].id == obj._id ){
                                loose.slice(idx,1);
                                break;
                            }
                        }
                    }
                }

                // 7. Re-link loose items to root
                if ( loose.length ){
                    var root_id = g_lib.getRootID(owner_id);
                    for ( i in loose )
                        g_db.item.save({ _from: root_id, _to: loose[i].id });
                }

                res.send( loose );
            }
        });
    } catch( e ) {
        g_lib.handleException( e, res );
    }
})
.queryParam('client', joi.string().required(), "Client ID")
.queryParam('id', joi.string().required(), "Collection ID or alias to modify")
.queryParam('add', joi.array().items(joi.string()).optional(), "Array of item IDs to add")
.queryParam('remove', joi.array().items(joi.string()).optional(), "Array of item IDs to remove")
.summary('Add/remove items in a collection')
.description('Add/remove items in a collection');

router.get('/move', function (req, res) {
    try {
        const client = g_lib.getUserFromClientID( req.queryParams.client );
        var src_id = g_lib.resolveCollID( req.queryParams.source, client );
        var src = g_db.c.document( src_id );
        var dst_id = g_lib.resolveCollID( req.queryParams.dest, client );
        var dst = g_db.c.document( dst_id );

        if ( src.owner != dst.owner )
            throw [g_lib.ERR_LINK,req.queryParams.source+" and "+req.queryParams.dest+" have different owners"];

        if ( !g_lib.hasAdminPermObject( client, src_id )) {
            if ( !g_lib.hasPermissions( client, src, g_lib.PERM_LINK | g_lib.PERM_SHARE ))
                throw g_lib.ERR_PERM_DENIED;
        }

        if ( !g_lib.hasAdminPermObject( client, dst_id )) {
            if ( !g_lib.hasPermissions( client, dst, g_lib.PERM_LINK ))
                throw g_lib.ERR_PERM_DENIED;
        }

        var i,item;

        for ( i in req.queryParams.items ) {
            // TODO - should aliases be resolved with client or owner ID?
            item = g_lib.getObject( req.queryParams.items[i], client );

            if ( item.is_root )
                throw [g_lib.ERR_LINK,"Cannot link root collection"];

            if ( !g_db.item.firstExample({ _from: src_id, _to: item._id }))
                throw [g_lib.ERR_UNLINK,item._id+" is not in collection " + src_id];

            if ( g_db.item.firstExample({ _from: dst_id, _to: item._id }))
                throw [g_lib.ERR_LINK,item._id+" is already in collection " + dst_id];

            if ( item._id[0] == "c" ){
                // Check for circular dependency
                if ( item._id == dst_id || g_lib.isSrcParentOfDest( item._id, dst_id ))
                    throw [g_lib.ERR_LINK,"Cannot link ancestor, "+item._id+", to descendant, "+dst_id];
            }

            g_db.item.removeByExample({ _from: src_id, _to: item._id });
            g_db.item.save({ _from: dst_id, _to: item._id });
        }

        res.send({});
    } catch( e ) {
        g_lib.handleException( e, res );
    }
})
.queryParam('client', joi.string().required(), "Client ID")
.queryParam('items', joi.array(joi.string()).optional(), "Items IDs/aliases to move")
.queryParam('source', joi.string().required(), "Source collection ID/alias" )
.queryParam('dest', joi.string().required(), "Destination collection ID/alias" )
.summary('Move items from source collection to destination collection')
.description('Move items from source collection to destination collection');

router.get('/get_parents', function (req, res) {
    try {
        const client = g_lib.getUserFromClientID( req.queryParams.client );
        var items, item_id = g_lib.resolveID( req.queryParams.id, client );

        // TODO Check non-owner permission for this?

        if ( req.queryParams.all ){
            items = g_db._query( "for v in 1..20 inbound @item item return { id: v._id, title: v.title, alias: v.alias }", { item: item_id }).toArray();
        }else{
            items = g_db._query( "for v in 1..1 inbound @item item return { id: v._id, title: v.title, alias: v.alias }", { item: item_id }).toArray();
        }

        res.send( items );
    } catch( e ) {
        g_lib.handleException( e, res );
    }
})
.queryParam('client', joi.string().required(), "Client ID")
.queryParam('id', joi.string().required(), "ID or alias of child item")
.queryParam('all', joi.boolean().optional(), "Get all parents (true), or just immediate (false, default)" )
.summary('Get parent collection(s) of item')
.description('Get parent collection(s) of item');
