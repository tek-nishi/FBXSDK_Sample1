//
// FBXを読み込む
//
// TODO:プロジェクト設定にFBX SDKのincludeファイルへのパスと
//      libファイルへのパスを追加する
//
// SOURCE:http://ramemiso.hateblo.jp/entry/2014/06/21/150405
// SOURCE:http://help.autodesk.com/view/FBX/2016/ENU/?guid=__files_GUID_105ED19A_9A5A_425E_BFD7_C1BBADA67AAB_htm
// SOURCE:http://shikemokuthinking.blogspot.jp/search/label/FBX
//

#ifdef _MSC_VER
// TIPS:FBX SDKはDLLを使う
#define FBXSDK_SHARED
#endif

#include <cinder/app/AppNative.h>
#include <cinder/Camera.h>
#include <cinder/Arcball.h>
#include <cinder/TriMesh.h>
#include <cinder/gl/gl.h>
#include <cinder/gl/Texture.h>
#include <cinder/gl/Light.h>
#include <cinder/gl/Material.h>
#include <cinder/ip/Flip.h>
#include <boost/optional.hpp>
#include <boost/filesystem.hpp>
#include <fbxsdk.h>                // FBX SDK

using namespace ci;
using namespace ci::app;


#ifdef _MSC_VER
// cp932 → UTF-8
std::string getUTF8Path(const std::string& path)
{
	// 相対パス → 絶対パス
	char fullPath[_MAX_PATH];
	_fullpath(fullPath, path.c_str(), _MAX_PATH);

	// cp932 → UTF8
	char* path_utf8;
	FbxAnsiToUTF8(fullPath, path_utf8);

	// char* → std::string
	std::string coverted_path(path_utf8);
	// FBX SDK内部で確保されたメモリは専用の関数で解放
	FbxFree(path_utf8);

	return coverted_path;
}
#endif


// FBXから取り出した情報を保持しておくための定義
// マテリアル
struct Material
{
  gl::Material material;
  boost::optional<gl::Texture> texture;
};


class TestProjectApp
  : public AppNative
{
  enum {
    WINDOW_WIDTH  = 800,
    WINDOW_HEIGHT = 600,
  };

  CameraPersp camera;

  Arcball arcball;


  // FBX情報
  FbxManager* manager;
  FbxScene* scene;
  FbxNode* root_node;

  // 表示物の情報を名前で管理
  std::map<std::string, TriMesh> meshes;
  std::map<std::string, Material> materials;


  gl::Light* light;


  TriMesh createMesh(FbxMesh* mesh);
  Material createMaterial(FbxSurfaceMaterial* material);

  // TIPS:FBXのデータを再帰的に描画する
  void draw(FbxNode* node);


public:
  void prepareSettings(Settings* settings);

  void mouseDown(MouseEvent event);
  void mouseDrag(MouseEvent event);

  void setup();
	void draw();
};


// FbxMesh→TriMesh
TriMesh TestProjectApp::createMesh(FbxMesh* mesh)
{
  TriMesh triMesh;

  {
    // 頂点配列
    int indexCount = mesh->GetPolygonVertexCount();
    console() << "index:" << indexCount << std::endl;

    // TIPS:FBXは保持している頂点座標数と法線数とUV数が一致しないので
    //      頂点配列から展開してTriMeshに格納している(T^T)
    int* index = mesh->GetPolygonVertices();
    for (int i = 0; i < indexCount; ++i)
    {
      auto controlPoint = mesh->GetControlPointAt(index[i]);
      triMesh.appendVertex(Vec3f(controlPoint[0], controlPoint[1], controlPoint[2]));
    }

    for (int i = 0; i < indexCount; i += 3)
    {
      triMesh.appendTriangle(i, i + 1, i + 2);
    }
  }

  {
    // 頂点法線
    FbxArray<FbxVector4> normals;
    mesh->GetPolygonVertexNormals(normals);

    console() << "normals:" << normals.Size() << std::endl;

    for (int i = 0; i < normals.Size(); ++i)
    {
      const FbxVector4& n = normals[i];
      triMesh.appendNormal(Vec3f(n[0], n[1], n[2]));
    }
  }

  {
    // UV
    FbxStringList uvsetName;
    mesh->GetUVSetNames(uvsetName);

    if (uvsetName.GetCount() > 0)
    {
      // 最初のUVセットを取り出す
      console() << "UV SET:" << uvsetName.GetStringAt(0) << std::endl;

      FbxArray<FbxVector2> uvsets;
      mesh->GetPolygonVertexUVs(uvsetName.GetStringAt(0), uvsets);

      console() << "UV:" << uvsets.Size() << std::endl;

      for (int i = 0; i < uvsets.Size(); ++i)
      {
        const FbxVector2& uv = uvsets[i];
        triMesh.appendTexCoord(Vec2f(uv[0], uv[1]));
      }
    }
  }

  return triMesh;
}

// FbxSurfaceMaterial → Material
Material TestProjectApp::createMaterial(FbxSurfaceMaterial* material)
{
  Material mat;

  ColorA ambient(0, 0, 0);
  ColorA diffuse(1, 1, 1);
  ColorA emissive(0, 0, 0);
  ColorA specular(0, 0, 0);
  float shininess = 80.0;

  // マテリアルから必要な情報を取り出す
  {
    FbxProperty prop = material->FindProperty(FbxSurfaceMaterial::sAmbient);
    if (prop.IsValid())
    {
      const auto& color = prop.Get<FbxDouble3>();
      ambient = ColorA(color[0], color[1], color[2]);
      console() << "ambient:" << ambient << std::endl;
    }
  }
  {
    FbxProperty prop = material->FindProperty(FbxSurfaceMaterial::sDiffuse);
    if (prop.IsValid())
    {
      const auto& color = prop.Get<FbxDouble3>();
      diffuse = ColorA(color[0], color[1], color[2]);
      console() << "diffuse:" << diffuse << std::endl;
    }
  }
  {
    FbxProperty prop = material->FindProperty(FbxSurfaceMaterial::sEmissive);
    if (prop.IsValid())
    {
      const auto& color = prop.Get<FbxDouble3>();
      emissive = ColorA(color[0], color[1], color[2]);
      console() << "emissive:" << emissive << std::endl;
    }
  }
  {
    FbxProperty prop = material->FindProperty(FbxSurfaceMaterial::sSpecular);
    if (prop.IsValid())
    {
      const auto& color = prop.Get<FbxDouble3>();
      specular = ColorA(color[0], color[1], color[2]);
      console() << "specular:" << specular << std::endl;
    }
  }
  {
    FbxProperty prop = material->FindProperty(FbxSurfaceMaterial::sShininess);
    if (prop.IsValid())
    {
      shininess = prop.Get<FbxDouble>();
      console() << "shininess:" << shininess << std::endl;
    }
  }

  mat.material = gl::Material(ambient, diffuse, specular, shininess, emissive);

  {
    // テクスチャ(Diffuseにアタッチされているテクスチャを取得)
    FbxProperty prop = material->FindProperty(FbxSurfaceMaterial::sDiffuse);
    if (prop.GetSrcObjectCount<FbxFileTexture>() > 0)
    {
      // TIPS:複数テクスチャが適用されてても無視して１枚目を使う
      FbxFileTexture* texture = prop.GetSrcObject<FbxFileTexture>(0);
      if (texture)
      {
        // TIPS:テクスチャのパスは絶対パスで格納されているので
        //      余分なディレクトリ名とかを削除している
        // TODO:複数のマテリアルで同じテクスチャが指定されている場合に対応
        fs::path name = texture->GetFileName();

        // TIPS:画像を読み込んで上下反転
        Surface surface = loadImage(loadAsset(name.filename()));
        ip::flipVertical(&surface);

        mat.texture = surface;
        mat.texture->setWrap(GL_REPEAT, GL_REPEAT);

        console() << "texture:" << name.filename() << std::endl;
      }
    }
  }

  return mat;
}


// 描画
void TestProjectApp::draw(FbxNode* node)
{
  // 行列
  FbxAMatrix& matrix = node->EvaluateGlobalTransform(0);

  // TIPS:１つのノードに複数のメッシュが含まれる
  int attr_count = node->GetNodeAttributeCount();
  for (int i = 0; i < attr_count; ++i)
  {
    FbxNodeAttribute* attr = node->GetNodeAttributeByIndex(i);
    switch(attr->GetAttributeType())
    {
    case FbxNodeAttribute::eMesh:
      {
        gl::pushModelView();

        glMatrixMode(GL_MODELVIEW);
        glMultMatrixd(matrix);

        // 描画に使うメッシュとマテリアルを特定
        FbxMesh* mesh = static_cast<FbxMesh*>(attr);
        const auto& tri_mesh = meshes.at(mesh->GetName());

        FbxSurfaceMaterial* material = node->GetMaterial(i);
        const auto& mat = materials.at(material->GetName());

        mat.material.apply();
        if (mat.texture)
        {
          mat.texture->enableAndBind();
        }

        gl::draw(tri_mesh);

        if (mat.texture)
        {
          mat.texture->unbind();
          mat.texture->disable();
        }

        gl::popModelView();
      }
      break;

    default:
      break;
    }
  }

  int childCount = node->GetChildCount();
  for (int i = 0; i < childCount; ++i)
  {
    FbxNode* child = node->GetChild(i);
    draw(child);
  }
}


void TestProjectApp::prepareSettings(Settings* settings)
{
  settings->setWindowSize(WINDOW_WIDTH, WINDOW_HEIGHT);
}

void TestProjectApp::setup()
{
  camera = CameraPersp(getWindowWidth(), getWindowHeight(),
                       35.0f,
                       0.1f, 100.0f);

  // TIPS:setEyePoint → setCenterOfInterestPoint の順で初期化すること
  camera.setEyePoint(Vec3f(0.0f, 0.0f, -4.0f));
  camera.setCenterOfInterestPoint(Vec3f(0.0f, 0.0f, 0.0f));

  light = new gl::Light(gl::Light::DIRECTIONAL, 0);
  light->setAmbient(Color(0.5f, 0.5f, 0.5f));
  light->setDiffuse(Color(0.8f, 0.8f, 0.8f));
  light->setSpecular(Color(0.8f, 0.8f, 0.8f));
  light->setDirection(Vec3f(0.0f, 0.0f, -1.0f));

  arcball = Arcball(Vec2i(WINDOW_WIDTH, WINDOW_HEIGHT));
  arcball.setRadius(150.0f);

  gl::enableDepthRead();
  gl::enableDepthWrite();
  gl::enable(GL_CULL_FACE);
  gl::enable(GL_NORMALIZE);
  gl::enableAlphaBlending();


  // FBXSDK生成
  manager = FbxManager::Create();
  assert(manager);

  // 読み込み機能を生成
  auto* importer = FbxImporter::Create(manager, "");
  assert(importer);

  // FBXファイルを読み込む
  // TIPS: getAssetPathは、assets内のファイルを探して、フルパスを取得する
  std::string path = getAssetPath("test2.fbx").string();
#ifdef _MSC_VER
  // cp932 → UTF-8
  path = getUTF8Path(path);
#endif

  //TIPS: std::string → char*
  if (!importer->Initialize(path.c_str()))
  {
    console() << "FBX:can't open " << path << std::endl;
  }

  // 読み込み用のシーンを生成
  scene = FbxScene::Create(manager, "");
  assert(scene);

  // ファイルからシーンへ読み込む
  importer->Import(scene);

  // FbxImporterはもう使わないのでここで破棄
  importer->Destroy();


  FbxGeometryConverter geometryConverter(manager);

  // TIPS:ポリゴンを全て三角形化
  geometryConverter.Triangulate(scene, true);

  // TIPS:マテリアルごとにメッシュを分割
  geometryConverter.SplitMeshesPerMaterial(scene, true);

  // FBX内の構造を取得しておく
  root_node = scene->GetRootNode();
  assert(root_node);

  {
    // シーンに含まれるメッシュの解析
    auto meshCount = scene->GetSrcObjectCount<FbxMesh>();
    console() << "meshCount:" << meshCount << std::endl;

    for (int i = 0; i < meshCount; ++i)
    {
      auto* mesh = scene->GetSrcObject<FbxMesh>(i);
      std::string name = mesh->GetName();

      if (meshes.count(name)) continue;

      TriMesh tri_mesh = createMesh(mesh);

      meshes.insert({ name, tri_mesh });
    }
  }

  {
    // シーンに含まれるマテリアルの解析
    auto materialCount = scene->GetMaterialCount();
    console() << "material:" << materialCount << std::endl;

    for (int i = 0; i < materialCount; ++i)
    {
      FbxSurfaceMaterial* material = scene->GetMaterial(i);
      std::string name = material->GetName();

      if (materials.count(name)) continue;

      Material mat = createMaterial(material);
      materials.insert({ name, mat });
    }
  }

  // FBS SDKを破棄
  // manager->Destroy();
}


void TestProjectApp::mouseDown(MouseEvent event)
{
  if (!event.isLeft()) return;

  Vec2i pos = event.getPos();
  arcball.mouseDown(pos);
}

void TestProjectApp::mouseDrag(MouseEvent event)
{
  if (!event.isLeftDown()) return;

  Vec2i pos = event.getPos();
  arcball.mouseDrag(pos);
}


void TestProjectApp::draw()
{
  gl::clear( Color( 0, 0, 0 ) );

  gl::setMatrices(camera);
  gl::enable(GL_LIGHTING);
  light->enable();

  {
    Quatf rotate = arcball.getQuat();
    Matrix44f m = rotate.toMatrix44();
    gl::multModelView(m);
  }

  gl::translate(0, 0, 0);
  gl::scale(1, 1, 1);

  // 描画
  draw(root_node);

  light->disable();
}

CINDER_APP_NATIVE( TestProjectApp, RendererGl )
