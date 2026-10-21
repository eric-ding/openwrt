'use strict';
/* LuCI API/DOM mocks: exercise the real view without a router or network. */
const fs = require('node:fs');
const assert = require('node:assert/strict');
const source = fs.readFileSync('config/njit/luci-app-njitclient/htdocs/luci-static/resources/view/network/njitclient.js', 'utf8');
function element(tag, attrs, children) {
 if (typeof attrs === 'string' || Array.isArray(attrs)) { children = attrs; attrs = {}; }
 attrs = attrs || {};
 const dataset = {};
 for (const [key, value] of Object.entries(attrs)) if (key.startsWith('data-')) dataset[key.slice(5)] = value;
 return { tag, attrs, children, dataset, value: attrs.value || '', checked: !!attrs.checked, disabled: !!attrs.disabled };
}
function descendants(node) {
 if (!node || typeof node !== 'object') return [];
 return [node, ...[node.children].flat(10).flatMap(descendants)];
}
const tick = () => new Promise(resolve => setImmediate(resolve));
async function setup(protocol = 'https:', permission = true) {
 const calls = [], notices = [], polls = [];
 let dialog;
 const rpc = { declare: spec => (...args) => {
  calls.push({ method: spec.method, args });
  if (spec.method === 'get_settings') return Promise.resolve({ ok: true, password_set: true, username: 'test user', domain: 'campus', autostart: false, reconnect: false });
  if (spec.method === 'status') return Promise.resolve({ ok: true, service_known: true, running: false, carrier_known: true, carrier: true, wan_known: true, wan_up: true, ipv4_addresses: ['192.0.2.10'] });
  return Promise.resolve({ ok: true });
 } };
 const ui = { addNotification: (...args) => notices.push(args), showModal: (title, children) => { dialog = { title, children }; }, hideModal: () => {} };
 const view = { extend: value => value };
 const L = { bind: (fn, ctx) => fn.bind(ctx), hasViewPermission: () => permission };
 const dom = { content: (node, children) => { node.children = children; } };
 const page = new Function('view', 'rpc', 'ui', 'poll', 'dom', 'L', 'E', 'location', 'TextEncoder', source)(view, rpc, ui, { add: fn => polls.push(fn) }, dom, L, element, { protocol }, TextEncoder);
 const tree = page.render(await page.load()); await tick();
 return { page, tree, calls, notices, polls, dialog: () => dialog, button: text => page.buttons.find(b => b.children === text) };
}
(async () => {
 const x = await setup();
 assert.equal(x.page.password.value, '');
 assert.equal(x.page.autostart.checked, false); assert.equal(x.page.reconnect.checked, false);
 assert.equal(x.calls.filter(c => ['save', 'control', 'save_options'].includes(c.method)).length, 0, 'render/poll must not change configuration or authenticate');
 assert.equal(x.polls.length, 1);
 x.page.password.value = "synthetic '$= password";
 await x.button('保存凭据').attrs.click(); await tick(); await tick();
 const save = x.calls.find(c => c.method === 'save');
 assert.deepEqual(save.args, ['test user', "synthetic '$= password", 'campus']);
 assert.equal(x.page.password.value, '');
 assert.equal(x.calls.filter(c => c.method === 'control').length, 0);
 x.page.username.value = '用'.repeat(30); x.page.domain.value = ''; x.page.password.value = 'x';
 x.button('保存凭据').attrs.click(); await tick();
 assert.equal(x.calls.filter(c => c.method === 'save').length, 1, 'enforce UTF-8 byte length');
 x.page.autostart.checked = true; x.page.reconnect.checked = true;
 x.button('保存自动认证选项').attrs.click();
 assert.equal(x.calls.filter(c => c.method === 'save_options').length, 0, 'require explicit confirmation of automatic retries');
 descendants(x.dialog()).find(n => n.tag === 'button' && n.children === '确认').attrs.click();
 await tick(); await tick();
 assert.deepEqual(x.calls.find(c => c.method === 'save_options').args, [true, true]);
 x.button('开始认证').attrs.click();
 assert.equal(x.calls.filter(c => c.method === 'control').length, 0);
 descendants(x.dialog()).find(n => n.tag === 'button' && n.children === '确认').attrs.click();
 await tick(); await tick();
 assert.deepEqual(x.calls.find(c => c.method === 'control').args, ['start']);
 const insecure = await setup('http:');
 assert.equal(insecure.page.password.disabled, true);
 assert.equal(insecure.button('开始认证').disabled, true);
 assert.equal(insecure.button('保存自动认证选项').disabled, true);
 assert.equal(insecure.button('停止认证').disabled, false);
 const readonly = await setup('https:', false);
 assert.ok(readonly.page.buttons.every(b => b.disabled));
 const acl = JSON.parse(fs.readFileSync('config/njit/luci-app-njitclient/root/usr/share/rpcd/acl.d/luci-app-njitclient.json'));
 const scope = acl['luci-app-njitclient'];
 assert.deepEqual(Object.keys(scope.read), ['ubus']); assert.deepEqual(Object.keys(scope.write), ['ubus']);
 assert.deepEqual(scope.read.ubus.njitclient, ['status', 'get_settings']);
 assert.deepEqual(scope.write.ubus.njitclient, ['save', 'clear', 'save_options', 'control']);
 assert.equal(JSON.stringify(acl).includes('*'), false);
 console.log('LuCI view checks passed: no automatic start, password blank/cleared, UTF-8 limits, separate options, confirmations, HTTPS gate, read-only controls and narrow ACL');
})().catch(error => { console.error(error); process.exitCode = 1; });
