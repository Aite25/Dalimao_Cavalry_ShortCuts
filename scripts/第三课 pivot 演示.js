// 第三课演示：pivot 在角上 vs 居中（左边甩出去，右边原地转）
// 运行后会删掉本脚本上次建的同名前缀图层，再重建一次。
// 在 Cavalry 里用 Scripts 菜单跑，或通过 MCP execute_script 跑都行。

const PREFIX = 'L3 ';
const comp = api.getActiveComp();

// —— 清掉上次的 ——
for (const id of api.getAllSceneLayers()) {
  if (api.getNiceName(id).startsWith(PREFIX)) api.deleteLayer(id);
}

// —— 先全部建出来，最后统一挂（api.parent 会改变"新建图层的落点"）——
const gC = api.create('group', PREFIX + 'Corner');
const gM = api.create('group', PREFIX + 'Centre');

const square = (x, y, name, colour) => {
  const r = api.primitive('rectangle', PREFIX + name);
  api.set(r, { 'generator.dimensions': [200, 200], position: [x, y] });
  api.set(r, { 'material.materialColor': colour });
  return r;
};
// 一个 L 形，占本地空间 0..340
const C = [square(80, 80, 'CA', '#F05A47'), square(240, 80, 'CB', '#F2C14E'), square(80, 240, 'CC', '#4EC5C1')];
const M = [square(80, 80, 'MA', '#F05A47'), square(240, 80, 'MB', '#F2C14E'), square(80, 240, 'MC', '#4EC5C1')];

const dot = () => {
  const e = api.primitive('ellipse', PREFIX + 'Dot');
  api.set(e, { 'generator.radius': [13, 13] });
  api.set(e, { 'material.materialColor': '#FFFFFF' });
  return e;
};
const dC = dot(), dM = dot();

const label = (txt, x, y, size) => {
  const t = api.create('textShape', PREFIX + txt);
  api.set(t, { text: txt, position: [x, y], fontSize: size });
  api.set(t, { 'material.materialColor': '#F2F2F2' });
  return t;
};
const lC = label('Pivot at the corner', -450, -400, 48);
const lM = label('Pivot centred', 330, -400, 48);

// —— 挂进各自的组 ——
C.forEach(k => api.parent(k, gC));
M.forEach(k => api.parent(k, gM));

// —— 两者几何从同一视觉位置出发，再转 45° ——
const baseL = { x: -450, y: -180 }, baseR = { x: 170, y: -180 };
api.set(gC, { pivot: [0, 0], position: [baseL.x, baseL.y], rotation: [0, 0, 45] });            // 绕左下角
api.set(gM, { pivot: [160, 160], position: [baseR.x + 160, baseR.y + 160], rotation: [0, 0, 45] }); // 绕中心

// —— pivot 标记点停在真正 pivot 的位置上 ——
const pC = api.getPivotPosition(gC, true), pM = api.getPivotPosition(gM, true);
api.set(dC, { position: [pC.x, pC.y] });
api.set(dM, { position: [pM.x, pM.y] });

// —— 标签对齐到各自形状下方（文字的 position 不等于 bbox 中心，得量一次） ——
const centreLabel = (t, tx, ty) => {
  api.set(t, { position: [tx, ty] });
  const bb = api.getBoundingBox(t, true);
  api.set(t, { position: [tx + (tx - bb.centre.x), ty + (ty - bb.centre.y)] });
};
centreLabel(lC, api.getBoundingBox(gC, true).centre.x, -400);
centreLabel(lM, api.getBoundingBox(gM, true).centre.x, -400);

// 提示：转 45° 后想看差别，把两组的 rotation.z 一起改（0 → 45 → 90），盯白点。
