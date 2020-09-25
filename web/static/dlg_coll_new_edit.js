import * as model from "./model.js";
import * as util from "./util.js";
import * as api from "./api.js";
import * as dialogs from "./dialogs.js";


export function show( a_data, a_parent, a_upd_perms, a_cb ){
    var ele = document.createElement('div');
    ele.id = (a_data?a_data.id.replace("/","_"):"c_new")+"_edit";
    var frame = $(ele),
        dlg_inst;

    frame.html( "<table class='form-table'>\
        <tr><td>Title: <span class='note'>*</span></td><td colspan='2'><input type='text' id='title' style='width:100%'></input></td></tr>\
        <tr><td>Alias:</td><td colspan='2'><input type='text' id='alias' style='width:100%'></input></td></tr>\
        <tr><td style='vertical-align:top'>Description:</td><td colspan='2'><textarea id='desc' rows=5 style='width:100%;padding:0'></textarea></td></tr>\
        <tr><td style='vertical-align:top'>Tags:</td><td colspan='2'><ul id='tags' class='content'></ul></td></tr>\
        <tr id='parent_row'><td>Parent: <span class='note'>*</span></td><td colspan='2'><input type='text' id='coll' style='width:100%'></input></td></tr>\
        <tr><td>Topic: <span class='note'>**</span></td><td><input title='Topic for publication' type='text' id='topic' style='width:100%'></input></td></tr></table>" );

    var dlg_title;
    if ( a_data ) {
        dlg_title = "Edit Collection " + a_data.id;
    } else {
        dlg_title = "New Collection";
    }

    var tag_el = $("#tags",frame);

    function callback( ok, reply ){
        if ( ok ) {
            dlg_inst.dialog('close');
    
            if ( a_cb )
                a_cb( reply.coll[0] );
        } else {
            dialogs.dlgAlert( "Collection " + (a_data?"Update":"Create") + " Error", reply );
        }
    }
    
    var options = {
        title: dlg_title,
        modal: false,
        width: 500,
        height: 'auto',
        resizable: true,
        position:{ my: "left", at: "center+10", of: "body" },
        buttons: [{
            text: "Cancel",
            click: function() {
                $(this).dialog('close');
            }
        },{
            text: a_data?"Update":"Create",
            click: function() {
                var obj = {};
                dlg_inst = $(this);

                if ( a_data ){
                    util.getUpdatedValue( $("#title",frame).val(), a_data, obj, "title" );
                    util.getUpdatedValue( $("#alias",frame).val(), a_data, obj, "alias" );
                    util.getUpdatedValue( $("#desc",frame).val(), a_data, obj, "desc" );
                    util.getUpdatedValue( $("#topic",frame).val().toLowerCase(), a_data, obj, "topic" );

                    // TODO Only assign tags if changed
                    obj.tags = tag_el.tagit("assignedTags");

                    if (( !obj.tags || obj.tags.length == 0 ) && a_data.tags && a_data.tags.length ){
                        obj.tagsClear = true;
                    }

                    if ( Object.keys(obj).length === 0 ){
                        $(this).dialog('close');
                        return;
                    }

                    obj.id = a_data.id;

                    api.collUpdate( obj, callback );
                }else{
                    obj.parentId = $("#coll",frame).val().trim();

                    util.getUpdatedValue( $("#title",frame).val(), {}, obj, "title" );
                    util.getUpdatedValue( $("#alias",frame).val(), {}, obj, "alias" );
                    util.getUpdatedValue( $("#desc",frame).val(), {}, obj, "desc" );
                    util.getUpdatedValue( $("#topic",frame).val().toLowerCase(), {}, obj, "topic" );

                    obj.tags = tag_el.tagit("assignedTags");

                    api.collCreate( obj, callback );
                }
            }
        }],
        open: function(){
            var widget = frame.dialog( "widget" );

            $(".ui-dialog-buttonpane",widget).append("<div style='font-size:85%' class='note'><span style='width:2em;display:inline-block;text-align:right'>*</span> Required fields<br><span style='width:2em;display:inline-block;text-align:right'>**</span> Enables anonymous read<div>");

            tag_el.tagit({
                autocomplete: {
                    delay: 500,
                    minLength: 3,
                    source: "/api/tag/autocomp"
                },
                caseSensitive: false
            });

            if ( a_data ){
                console.log("coll data:",a_data);
                $("#title",frame).val(a_data.title);
                if ( a_data.alias ){
                    var idx =  a_data.alias.lastIndexOf(":");
                    a_data.alias = (idx==-1?a_data.alias:a_data.alias.substr(idx+1));
                    $("#alias",frame).val(a_data.alias);
                }
                $("#desc",frame).val(a_data.desc);
                $("#parent_row",frame).css("display","none");
                $("#topic",frame).val(a_data.topic);

                if (( a_upd_perms & model.PERM_WR_REC ) == 0 ){
                    util.inputDisable( $("#title,#desc,#alias", frame ));
                }

                if (( a_upd_perms & model.PERM_SHARE ) == 0 ){
                    util.inputDisable( $("#topic", frame ));
                }

                if ( a_data.tags && a_data.tags.length ){
                    for ( var t in a_data.tags ){
                        tag_el.tagit("createTag", a_data.tags[t] );
                    }
                }
            } else {
                $("#title",frame).val("");
                $("#alias",frame).val("");
                $("#desc",frame).val("");
                $("#coll",frame).val(a_parent?a_parent:"");
                $("#topic",frame).val("");
            }

            util.inputTheme($('input',frame));
            util.inputTheme($('textarea',frame));
            $(".btn",frame).button();
        },
        close: function( ev, ui ) {
            $(this).dialog("destroy").remove();
        }
    };

    frame.dialog( options );
}
