//
// Copyright (C) 2015 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "attestation/server/attestation_service.h"

#include <algorithm>
#include <climits>
#include <set>
#include <string>
#include <vector>

#include <attestation/proto_bindings/attestation_ca.pb.h>
#include <base/bind.h>
#include <base/callback.h>
#include <base/sha1.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <brillo/cryptohome.h>
#include <brillo/data_encoding.h>
#include <crypto/sha2.h>
#include <openssl/objects.h>
#if USE_TPM2
#include <trunks/tpm_utility.h>
#endif
extern "C" {
#if USE_TPM2
#include <trunks/cr50_headers/virtual_nvmem.h>
#endif
#include <vboot/crossystem.h>
}

#include "attestation/common/database.pb.h"
#include "attestation/common/tpm_utility_factory.h"
#include "attestation/server/database_impl.h"

namespace {

// Google Attestation Certificate Authority (ACA) production instance.
// https://chromeos-ca.gstatic.com
const char kDefaultACAPublicKey[] =
    "A2976637E113CC457013F4334312A416395B08D4B2A9724FC9BAD65D0290F39C"
    "866D1163C2CD6474A24A55403C968CF78FA153C338179407FE568C6E550949B1"
    "B3A80731BA9311EC16F8F66060A2C550914D252DB90B44D19BC6C15E923FFCFB"
    "E8A366038772803EE57C7D7E5B3D5E8090BF0960D4F6A6644CB9A456708508F0"
    "6C19245486C3A49F807AB07C65D5E9954F4F8832BC9F882E9EE1AAA2621B1F43"
    "4083FD98758745CBFFD6F55DA699B2EE983307C14C9990DDFB48897F26DF8FB2"
    "CFFF03E631E62FAE59CBF89525EDACD1F7BBE0BA478B5418E756FF3E14AC9970"
    "D334DB04A1DF267D2343C75E5D282A287060D345981ABDA0B2506AD882579FEF";
const char kDefaultACAPublicKeyID[] = "\x00\xc7\x0e\x50\xb1";

// Google Attestation Certificate Authority (ACA) test instance.
// https://asbestos-qa.corp.google.com
const char kTestACAPublicKey[] =
    "A1D50D088994000492B5F3ED8A9C5FC8772706219F4C063B2F6A8C6B74D3AD6B"
    "212A53D01DABB34A6261288540D420D3BA59ED279D859DE6227A7AB6BD88FADD"
    "FC3078D465F4DF97E03A52A587BD0165AE3B180FE7B255B7BEDC1BE81CB1383F"
    "E9E46F9312B1EF28F4025E7D332E33F4416525FEB8F0FC7B815E8FBB79CDABE6"
    "327B5A155FEF13F559A7086CB8A543D72AD6ECAEE2E704FF28824149D7F4E393"
    "D3C74E721ACA97F7ADBE2CCF7B4BCC165F7380F48065F2C8370F25F066091259"
    "D14EA362BAF236E3CD8771A94BDEDA3900577143A238AB92B6C55F11DEFAFB31"
    "7D1DC5B6AE210C52B008D87F2A7BFF6EB5C4FB32D6ECEC6505796173951A3167";
const char kTestACAPublicKeyID[] = "\x00\xc2\xb0\x56\x2d";

#ifdef USE_TEST_ACA
#error "Do not compile with USE_TEST_ACA"
       " but provide the right aca_type in requests."
#endif

const char kDefaultEnterpriseSigningPublicKey[] =
    "bf7fefa3a661437b26aed0801db64d7ba8b58875c351d3bdc9f653847d4a67b3"
    "b67479327724d56aa0f71a3f57c2290fdc1ff05df80589715e381dfbbda2c4ac"
    "114c30d0a73c5b7b2e22178d26d8b65860aa8dd65e1b3d61a07c81de87c1e7e4"
    "590145624936a011ece10434c1d5d41f917c3dc4b41dd8392479130c4fd6eafc"
    "3bb4e0dedcc8f6a9c28428bf8fbba8bd6438a325a9d3eabee1e89e838138ad99"
    "69c292c6d9f6f52522333b84ddf9471ffe00f01bf2de5faa1621f967f49e158b"
    "f2b305360f886826cc6fdbef11a12b2d6002d70d8d1e8f40e0901ff94c203cb2"
    "01a36a0bd6e83955f14b494f4f2f17c0c826657b85c25ffb8a73599721fa17ab";
const char kDefaultEnterpriseEncryptionPublicKey[] =
    "edba5e723da811e41636f792c7a77aef633fbf39b542aa537c93c93eaba7a3b1"
    "0bc3e484388c13d625ef5573358ec9e7fbeb6baaaa87ca87d93fb61bf5760e29"
    "6813c435763ed2c81f631e26e3ff1a670261cdc3c39a4640b6bbf4ead3d6587b"
    "e43ef7f1f08e7596b628ec0b44c9b7ad71c9ee3a1258852c7a986c7614f0c4ec"
    "f0ce147650a53b6aa9ae107374a2d6d4e7922065f2f6eb537a994372e1936c87"
    "eb08318611d44daf6044f8527687dc7ce5319b51eae6ab12bee6bd16e59c499e"
    "fa53d80232ae886c7ee9ad8bc1cbd6e4ac55cb8fa515671f7e7ad66e98769f52"
    "c3c309f98bf08a3b8fbb0166e97906151b46402217e65c5d01ddac8514340e8b";
const char kDefaultEnterpriseEncryptionPublicKeyID[] =
    "\x00\x4a\xe2\xdc\xae";

const char kTestEnterpriseSigningPublicKey[] =
    "baab3e277518c65b1b98290bb55061df9a50b9f32a4b0ff61c7c61c51e966fcd"
    "c891799a39ee0b7278f204a2b45a7e615080ff8f69f668e05adcf3486b319f80"
    "f9da814d9b86b16a3e68b4ce514ab5591112838a68dc3bfdcc4043a5aa8de52c"
    "ae936847a271971ecaa188172692c13f3b0321239c90559f3b7ba91e66d38ef4"
    "db4c75104ac5f2f15e55a463c49753a88e56906b1725fd3f0c1372beb16d4904"
    "752c74452b0c9f757ee12877a859dd0666cafaccbfc33fe67d98a89a2c12ef52"
    "5e4b16ea8972577dbfc567c2625a3eee6bcaa6cb4939b941f57236d1d57243f8"
    "c9766938269a8034d82fbd44044d2ee6a5c7275589afc3790b60280c0689900f";
const char kTestEnterpriseEncryptionPublicKey[] =
    "c0c116e7ded8d7c1e577f9c8fb0d267c3c5c3e3b6800abb0309c248eaa5cd9bf"
    "91945132e4bb0111711356a388b756788e20bc1ecc9261ea9bcae8369cfd050e"
    "d8dc00b50fbe36d2c1c8a9b335f2e11096be76bebce8b5dcb0dc39ac0fd963b0"
    "51474f794d4289cc0c52d0bab451b9e69a43ecd3a84330b0b2de4365c038ffce"
    "ec0f1999d789615849c2f3c29d1d9ed42ccb7f330d5b56f40fb7cc6556190c3b"
    "698c20d83fb341a442fd69701fe0bdc41bdcf8056ccbc8d9b4275e8e43ec6b63"
    "c1ae70d52838dfa90a9cd9e7b6bd88ed3abf4fab444347104e30e635f4f296ac"
    "4c91939103e317d0eca5f36c48102e967f176a19a42220f3cf14634b6773be07";
const char kTestEnterpriseEncryptionPublicKeyID[] =
    "\x00\xef\x22\x0f\xb0";

const size_t kNonceSize = 20;  // As per TPM_NONCE definition.
const int kNumTemporalValues = 5;

const char kKnownBootModes[8][3] = {
  {0, 0, 0}, {0, 0, 1},
  {0, 1, 0}, {0, 1, 1},
  {1, 0, 0}, {1, 0, 1},
  {1, 1, 0}, {1, 1, 1}
};
const char kVerifiedBootMode[3] = {0, 0, 1};

// Context name to derive stable secret for attestation-based enterprise
// enrollment.
const char kAttestationBasedEnterpriseEnrollmentContextName[] =
    "attestation_based_enrollment";

struct CertificateAuthority {
  const char* issuer;
  const char* modulus;  // In hex format.
};

const CertificateAuthority kKnownEndorsementCA[] = {
  { "IFX TPM EK Intermediate CA 06",
    "de9e58a353313d21d683c687d6aaaab240248717557c077161c5e515f41d8efa"
    "48329f45658fb550f43f91d1ba0c2519429fb6ef964f89657098c90a9783ad6d"
    "3baea625db044734c478768db53b6022c556d8174ed744bd6e4455665715cd5c"
    "beb7c3fcb822ab3dfab1ecee1a628c3d53f6085983431598fb646f04347d5ae0"
    "021d5757cc6e3027c1e13f10633ae48bbf98732c079c17684b0db58bd0291add"
    "e277b037dd13fa3db910e81a4969622a79c85ac768d870f079b54c2b98c856e7"
    "15ef0ba9c01ee1da1241838a1307fe94b1ddfa65cdf7eeaa7e5b4b8a94c3dcd0"
    "29bb5ebcfc935e56641f4c8cb5e726c68f9dd6b41f8602ef6dc78d870a773571" },
  { "IFX TPM EK Intermediate CA 07",
    "f04c9b5b9f3cbc2509179f5e0f31dceb302900f528458e002c3e914d6b29e5e0"
    "924b0bcab2dd053f65d9d4a8eea8269c85c419dba640a88e14dc5f8c8c1a4269"
    "7a5ac4594b36f923110f91d1803d385540c01a433140b06054c77a144ee3a6a6"
    "5950c20f9215be3473b1002eb6b1756a22fbc18d21efacbbc8c270c66cf74982"
    "e24f057825cab51c0dd840a4f2d059032239c33e3f52c6ca06fe49bf4f60cc28"
    "a0fb1173d2ee05a141d30e8ffa32dbb86c1aeb5b309f76c2e462965612ec929a"
    "0d3b04acfa4525912c76f765e948be71f505d619cc673a889f0ed9e1d75f237b"
    "7af6a68550253cb4c3a8ff16c8091dbcbdea0ff8eee3d5bd92f49c53c5a15c93" },
  { "IFX TPM EK Intermediate CA 14",
    "D5B2EB8F8F23DD0B5CA0C15D4376E27A0380FD8EB1E52C2C270D961E8C0F66FD"
    "62E6ED6B3660FFBD8B0735179476F5E9C2EA4C762F5FEEDD3B5EB91785A724BC"
    "4C0617B83966336DD9DC407640871BF99DF4E1701EB5A1F5647FC57879CBB973"
    "B2A72BABA8536B2646A37AA5B73E32A4C8F03E35C8834B391AD363F1F7D1DF2B"
    "EE39233F47384F3E2D2E8EF83C9539B4DFC360C8AEB88B6111E757AF646DC01A"
    "68DAA908C7F8068894E9E991C59005068DD9B0F87113E6A80AB045DB4C1B23FF"
    "38A106098C2E184E1CF42A43EA68753F2649999048E8A3C3406032BEB1457070"
    "BCBE3A93E122638F6F18FF505C35FB827CE5D0C12F27F45C0F59C8A4A8697849" },
  { "IFX TPM EK Intermediate CA 16",
    "B98D42D5284620036A6613ED05A1BE11431AE7DE435EC55F72814652B9265EC2"
    "9035D401B538A9C84BB5B875450FAE8FBEDEF3430C4108D8516404F3DE4D4615"
    "2F471013673A7C7F236304C7363B91C0E0FD9FC7A9EC751521A60A6042839CF7"
    "7AEDE3243D0F51F47ACC39676D236BD5298E18B9A4783C60B2A1CD1B32124909"
    "D5844649EE4539D6AA05A5902C147B4F062D5145708EAE224EC65A8B51D7A418"
    "6327DA8F3B9E7C796F8B2DB3D2BDB39B829BDEBA8D2BF882CBADDB75D76FA8FA"
    "313682688BCD2835533A3A68A4AFDF7E597D8B965402FF22A5A4A418FDB4B549"
    "F218C3908E66BDCEAB3E2FE5EE0A4A1D9EB41A286ED07B6C112581FDAEA088D9" },
  { "IFX TPM EK Intermediate CA 17",
    "B0F3CC6F02E8C0486501102731069644A815F631ED41676C05CE3F7E5E5E40DF"
    "B3BF6D99787F2A9BE8F8B8035C03D5C2226072985230D4CE8407ACD6403F72E1"
    "A4DBF069504E56FA8C0807A704526EAC1E379AE559EB4BBAD9DB4E652B3B14E5"
    "38497A5E7768BCE0BFFAF800C61F1F2262775C526E1790A2BECF9A072A58F6A0"
    "F3042B5279FE9957BCADC3C9725428B66B15D5263F00C528AC47716DE6938199"
    "0FF23BC28F2C33B72D89B5F8EEEF9053B60D230431081D656EA8EC16C7CEFD9E"
    "F5A9061A3C921394D453D9AC77397D59B4C3BAF258266F65559469C3007987D5"
    "A8338E10FC54CD930303C37007D6E1E6C63F36BCFBA1E494AFB3ECD9A2407FF9" },
  { "IFX TPM EK Intermediate CA 21",
    "8149397109974D6C0850C8A60304ED7D209B1B88F435B695394DAD9FB4E64180"
    "02A3940966D2F04103C88659600EEA8E2A5C697C5F989F62D33A06DA10B50075"
    "F37F3CE6AD070413A0E109E16FE652B393C4DAFC5579CCB9915E9A70F5C05BCE"
    "0D341D6B887F43C4334BD8EC6A293FFAB737F77A45069CD0345D3D534E84D029"
    "029C37A267C0CC2D8DCE3E2C76F21A40F5D8D463882A8CBB92D8235685266753"
    "E8F051E78B681E87810A5B21EF719662A8208DFD94C55A126A112E39E0D732D7"
    "3C599095FAFF52BBC0E8C5B3DCD904D05DE00D5C5112F3DF7B76602ABE5DC0F8"
    "F89B55889A24C54EDBA1234AE498BE9B02CB5C8048D1DC90210705BAFC0E2837" },
  { "IFX TPM EK Intermediate CA 29",
    "cd424370776890ace339c62d7faae843bb2c765d27685c0441d278361a929062"
    "b4c95cc57213c864e91cbb92b1151f17a346a4e754c666f2a3e07ea9ffb9c80f"
    "e54d9479f73458c64bf7b0ca4e38821dd318e82d6fe387903ca73ca3e59db48e"
    "fe3b3c7c89599be87bb5e439a6f5843a412d4a321f154955448b71ca0b5fda47"
    "5c86a1c999dde7a01aa16436e65f0b04874c0db3970546bd806157058c5576a5"
    "c00b2bce7173c887f388dc4d5267c68fa5c47fcee3d8491071cd7742d43162cb"
    "285f5ba5e0daa0e910fdce566c5bbf7b3701d51660090344195fd7278456bd98"
    "48382fc5fceaebf93a2ec88c5722723519692e90d23f869c34d8b1af499d4127" },
  { "IFX TPM EK Intermediate CA 30",
    "a01cc43c4b66076d483086d0713a336f435e33ed23d3cda05f3c60a6f707416a"
    "9e53f0ef0de62c82a720e9ad94df29805b56b44279fd7389de4c60d498c81e3b"
    "a27692a045d993e9aaae152768588e5c62213721154529c95b09b201bcb3e573"
    "3d98e398d6e05215867d94e3d222e5b7df9f948c14533285821658b282be4bd7"
    "fe7197baa642f556d4f18738adef26b2eebfc64045cf4c5dcbff661aa95429f4"
    "e2c4921a8723bd8116f0efc038cd4530bb6e9299b7d70327e3fe8790d3d6db3a"
    "ebd3ccd12aef3d43cf89463a28ad1306a9d430b08c3411bfeeda63b9fdcc9a23"
    "1ff5cc203a7f5ee713d50e1930add1cd32ff64637fc740edb63380a5e6725381" },
  { "NTC TPM EK Root CA 01",
    "e836ac61b43e3252d5e1a8a4061997a6a0a272ba3d519d6be6360cc8b4b79e8c"
    "d53c07a7ce9e9310ca84b82bbdad32184544ada357d458cf224c4a3130c97d00"
    "4933b5db232d8b6509412eb4777e9e1b093c58b82b1679c84e57a6b218b4d61f"
    "6dd4c3a66b2dd33b52cb1ffdff543289fa36dd71b7c83b66c1aae37caf7fe88d"
    "851a3523e3ea92b59a6b0ca095c5e1d191484c1bff8a33048c3976e826d4c12a"
    "e198f7199d183e0e70c8b46e8106edec3914397e051ae2b9a7f0b4bb9cd7f2ed"
    "f71064eb0eb473df27b7ccef9a018d715c5fe6ab012a8315f933c7f4fc35d34c"
    "efc27de224b2e3de3b3ba316d5df8b90b2eb879e219d270141b78dbb671a3a05" },
  { "STM TPM EK Intermediate CA 03",
    "a5152b4fbd2c70c0c9a0dd919f48ddcde2b5c0c9988cff3b04ecd844f6cc0035"
    "6c4e01b52463deb5179f36acf0c06d4574327c37572292fcd0f272c2d45ea7f2"
    "2e8d8d18aa62354c279e03be9220f0c3822d16de1ea1c130b59afc56e08f22f1"
    "902a07f881ebea3703badaa594ecbdf8fd1709211ba16769f73e76f348e2755d"
    "bba2f94c1869ef71e726f56f8ece987f345c622e8b5c2a5466d41093c0dc2982"
    "e6203d96f539b542347a08e87fc6e248a346d61a505f52add7f768a5203d70b8"
    "68b6ec92ef7a83a4e6d1e1d259018705755d812175489fae83c4ab2957f69a99"
    "9394ac7a243a5c1cd85f92b8648a8e0d23165fdd86fad06990bfd16fb3293379" },
  /* TODO(ngm): remove by: Aug 30 08:44:33 2020 GMT */
  { "CROS TPM DEV EK ROOT CA",
    "cdc108745dc50dd6a1098c31486fb31578607fd64f64b0d91b994244ca1a9a69"
    "a74c6bccc7f24923e1513e132dc0d9dbcb1b22089299bb6cb669cbf4b704c992"
    "27bb769fa1f91ab11f67fb464a065b34b1a0e824136af5e59d1ac04bda22c199"
    "9f7a5b34bd6b50c81b4a88cc097d4dfeb4dc695096463d9529d69f116e2a26de"
    "070ef3118287072bdbe94466b8737049809bb8e1276b245930051b2bbbad71dd"
    "20d26349d1d83cdb2ff9c65251a17dae4f400ecc3e77f89e27a75fe0709dc81f"
    "e172008a3e65de685d9df43e036c557e88f1a9aedf7a91644391523d9728f946"
    "45c0e8adaf37e9a15777021ad43b675583302402912d66233c59ad05fa3b34ed"
  },
  { "CROS TPM PRD EK ROOT CA",
    "bd6f0198ffa7f7d20c15f81642096e335e2cd74734f73008265fc9957bbe018d"
    "fbac0d2a0ea99f5fb7bbff6f0d367b81199e837c390527972aa5392c2ca0f2a3"
    "506ee7d4a938f47158a7c56a390df2b781344a82b885a62f1de78f37ec105749"
    "69d8abf3163f0cf5c67fa05dd4fb3eb07a7571888b7a87ed57735ce476156bf7"
    "d6eff6cb8c8b303c21ebfe0e11b660edbdf903c70ac16927345d0b38c72f1e60"
    "1460743584f5a3eaef303dbc5cfda48e4c7a1f338108c7f0c70a694f814b6691"
    "ba9d058ab988152bb7097a010e400462187811c3e062001bce8aa808db485bd8"
    "2f7f0e1e2a2ddb95c364dffea4c23e872fc3874c4756e85e6cf8eca6eb6a07bf"
  },
};

const CertificateAuthority kKnownCrosCoreEndorsementCA[] = {
  { "IFX TPM EK Intermediate CA 24",
    "9D3F39677EBDB7B95F383021EA6EF90AD2BEA4E38B10CA65DCD84D0B33D400FA"
    "E7E56FC553975FDADD425227F055C029B6544331E3BA50ED33F6CC02D833EA4E"
    "0EECFE9AD1ADD7095F3A804C560F031E8705A3AD5189CBD62678B5B8205C37ED"
    "780A3EDE8DE64A08980C048872E789937A49FC4048EADCAC9B3FD0F0DD085E76"
    "30DDF9C0C31EFF3B77C6C3601AA7C3DCD10F08616C01435697746A61F920335C"
    "0C45A41149F5D22FCD23DBE35003A9AF7FD91C18715E3709F86A38AB149113C4"
    "D5273C3C90599734FF627ACBF408B082C76E486091F27446E175C50D340DA0FE"
    "5C3FE3D590B8729F4E364E5BF7D854D9AE28EFBCD0CE8F19E6462B3A593983DF" },
  { "IFX TPM EK Intermediate CA 50",
    "ACB01856664D0C81B545DB926D25019FC2D06B4A97DFB91FD7A5AB1A803AA6F4"
    "12FEEE5E3DEF3634172F1271E893C6848B4D156485917DF6F0504947B39F0A5A"
    "E14FFBAB9FF00E70448E51F11DEEA1EA16287ABAAE05D3D00FEB1AA064F1CBD9"
    "E1E67C057087110F9D3023BFA0545C97BD51E473C5B183E50C2984BD9A2DA39B"
    "7D028B895BD939FF0822595DDC948640D06E57ED72EF43B8D8071D2C3C0497A0"
    "EC52F682D1637F06979733BAF56DD809D24C20354D73D3849A1C0DAD23AD5CCB"
    "F8C679242D13FFFE055CC2AB2692897F0329EEA55AF3BB10A4EB4E2937601196"
    "90D64FB352E3D34E05AB53BD4E01EFE3EF56F6DBE315B76A31B0100BF7096093" },
};

// Returns a human-readable description for a known 3-byte |mode|.
std::string GetDescriptionForMode(const char* mode) {
  return base::StringPrintf(
      "Developer Mode: %s, Recovery Mode: %s, Firmware Type: %s",
      mode[0] ? "On" : "Off",
      mode[1] ? "On" : "Off",
      mode[2] ? "Verified" : "Developer");
}

std::string GetHardwareID() {
  char buffer[VB_MAX_STRING_PROPERTY];
  const char* property =
      VbGetSystemPropertyString("hwid", buffer, arraysize(buffer));
  if (property != nullptr) {
    return std::string(property);
  }
  LOG(WARNING) << "Could not read hwid property.";
  return std::string();
}

// Finds CA by |issuer_name| and |is_cros_core| flag. On success returns true
// and fills |public_key_hex| with CA public key hex modulus.
bool GetAuthorityPublicKey(
    const std::string& issuer_name,
    bool is_cros_core,
    std::string* public_key_hex) {
  const CertificateAuthority* const kKnownCA =
      is_cros_core ? kKnownCrosCoreEndorsementCA : kKnownEndorsementCA;
  const int kNumIssuers =
      is_cros_core ? arraysize(kKnownCrosCoreEndorsementCA) :
                     arraysize(kKnownEndorsementCA);
  for (int i = 0; i < kNumIssuers; ++i) {
    if (issuer_name == kKnownCA[i].issuer) {
      public_key_hex->assign(kKnownCA[i].modulus);
      return true;
    }
  }
  return false;
}

std::string GetACAName(attestation::ACAType aca_type) {
  switch (aca_type) {
    case attestation::DEFAULT_ACA:
      return "the default ACA";
    case attestation::TEST_ACA:
      return "the test ACA";
    default: {
      std::ostringstream stream;
      stream << "ACA " << aca_type;
      return stream.str();
    }
  }
}

std::string GetIdentityFeaturesString(int identity_features) {
  unsigned features_count = 0;
  std::ostringstream stream;
  if (identity_features == attestation::NO_IDENTITY_FEATURES) {
    stream << "NO_IDENTITY_FEATURES";
  } else {
    // We don't have reflection, copy/paste and adapt these few lines when
    // adding a new enum value.
    if (identity_features &
        attestation::IDENTITY_FEATURE_ENTERPRISE_ENROLLMENT_ID) {
      ++features_count;
      if (stream.tellp() > 0) {
        stream << ", ";
      }
      stream << "IDENTITY_FEATURE_ENTERPRISE_ENROLLMENT_ID";
      identity_features &=
          ~attestation::IDENTITY_FEATURE_ENTERPRISE_ENROLLMENT_ID;
    }
    // Print other bits which may have been forgotten above.
    if (identity_features) {
      features_count += 2;        // Forces plural.
      if (stream.tellp() > 0) {
        stream << ", ";
      }
      stream << "(undecoded features: " << identity_features << ")";
    }
  }
  return std::string("identity feature") + (features_count != 1 ? "s " : " ")
      + stream.str();
}

std::string GetKeyTypeName(attestation::KeyType key_type) {
  switch (key_type) {
    case attestation::KEY_TYPE_ECC:
      return "ECC";
    case attestation::KEY_TYPE_RSA:
      return "RSA";
  }
  return "unknown";
}

void LogErrorFromCA(const std::string& func, const std::string& details,
                    const std::string& extra_details) {
  std::ostringstream stream;
  stream << func << ": Received error from Attestation CA";
  if (!details.empty()) {
    stream << ": " << details;
    if (!extra_details.empty()) {
      stream << ". Extra details: " << extra_details;
    }
  }
  LOG(ERROR) << stream.str() << ".";
}

}  // namespace

namespace attestation {

// Last PCR index to quote (we start at 0).
constexpr int kLastPcrToQuote = 1;

#if USE_TPM2

// Description of virtual NVRAM indices used for attestation.
const struct {
  NVRAMQuoteType quote_type;
  const char* quote_name;
  uint32_t nv_index;            // From CertifyNV().
  uint16_t nv_size;             // From CertifyNV().
} kNvramIndexData[] = {
  {
    BOARD_ID,
    "BoardId",
    VIRTUAL_NV_INDEX_BOARD_ID,
    VIRTUAL_NV_INDEX_BOARD_ID_SIZE
  },
  {
    SN_BITS,
    "SN Bits",
    VIRTUAL_NV_INDEX_SN_DATA,
    VIRTUAL_NV_INDEX_SN_DATA_SIZE
  },
  {
    RSA_PUB_EK_CERT,
    "RSA Public EK Certificate",
    trunks::kRsaEndorsementCertificateIndex,
    0
  },
  {
    RSU_DEVICE_ID,
    "RSU Device ID",
    VIRTUAL_NV_INDEX_RSU_DEV_ID,
    VIRTUAL_NV_INDEX_RSU_DEV_ID_SIZE
  }
};

// Types of quotes being cached in the identity data.
const NVRAMQuoteType kNvramQuoteTypeInIdentityData[] = {
  BOARD_ID,
  SN_BITS
};

// Types of quotes needed to obtain an enrollment certificate.
const NVRAMQuoteType kNvramQuoteTypeForEnrollmentCertificate[] = {
  BOARD_ID,
  SN_BITS,
  RSU_DEVICE_ID
};

#endif

using QuoteMap = google::protobuf::Map<int, Quote>;

const size_t kChallengeSignatureNonceSize = 20;  // For all TPMs.

AttestationService::AttestationService(brillo::SecureBlob* abe_data)
    : abe_data_(abe_data), weak_factory_(this) {}

bool AttestationService::Initialize() {
  if (!worker_thread_) {
    worker_thread_.reset(new ServiceWorkerThread(this));
    worker_thread_->StartWithOptions(
        base::Thread::Options(base::MessageLoop::TYPE_IO, 0));
    LOG(INFO) << "Attestation service started.";
  }
  worker_thread_->task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&AttestationService::InitializeTask, base::Unretained(this)));
  return true;
}

void AttestationService::InitializeTask() {
  if (!tpm_utility_) {
    default_tpm_utility_.reset(TpmUtilityFactory::New());
    CHECK(default_tpm_utility_->Initialize());
    tpm_utility_ = default_tpm_utility_.get();
  }
  if (!crypto_utility_) {
    default_crypto_utility_.reset(new CryptoUtilityImpl(tpm_utility_));
    crypto_utility_ = default_crypto_utility_.get();
  }
#if USE_TPM2
  for (int i = 0; i < arraysize(kNvramIndexData); ++i) {
    nvram_quote_type_to_index_data_[kNvramIndexData[i].quote_type] = i;
  }
#endif
  bool existing_database;
  if (database_) {
    existing_database = true;
  } else {
    default_database_.reset(new DatabaseImpl(crypto_utility_));
    existing_database = default_database_->Initialize();
    database_ = default_database_.get();
  }
  if (existing_database && MigrateAttestationDatabase()) {
    if (!database_->SaveChanges()) {
      LOG(WARNING) << "Attestation: Failed to persist database changes.";
    }
  }
  if (!key_store_) {
    pkcs11_token_manager_.reset(new chaps::TokenManagerClient());
    default_key_store_.reset(new Pkcs11KeyStore(pkcs11_token_manager_.get()));
    key_store_ = default_key_store_.get();
  }
  if (hwid_.empty()) {
    hwid_ = GetHardwareID();
  }
  if (!IsPreparedForEnrollment()) {
    worker_thread_->task_runner()->PostTask(
        FROM_HERE, base::Bind(&AttestationService::PrepareForEnrollment,
                              base::Unretained(this)));
  } else {
    // Ignore errors. If failed this time, will be re-attempted on next boot.
    tpm_utility_->RemoveOwnerDependency();
  }
}

bool AttestationService::MigrateAttestationDatabase() {
  bool migrated = false;

  auto* database_pb = database_->GetMutableProtobuf();
  if (database_pb->has_credentials()) {
    if (!database_pb->credentials().encrypted_endorsement_credentials().count(
            DEFAULT_ACA) &&
        database_pb->credentials()
            .has_default_encrypted_endorsement_credential()) {
      LOG(INFO) << "Attestation: Migrating endorsement credential for "
                << GetACAName(DEFAULT_ACA) << ".";
      (*database_pb->mutable_credentials()
            ->mutable_encrypted_endorsement_credentials())[DEFAULT_ACA] =
          database_pb->credentials().default_encrypted_endorsement_credential();
      migrated = true;
    }
    if (!database_pb->credentials().encrypted_endorsement_credentials().count(
            TEST_ACA) &&
        database_pb->credentials()
            .has_test_encrypted_endorsement_credential()) {
      LOG(INFO) << "Attestation: Migrating endorsement credential for "
                << GetACAName(TEST_ACA) << ".";
      (*database_pb->mutable_credentials()
            ->mutable_encrypted_endorsement_credentials())[TEST_ACA] =
          database_pb->credentials().test_encrypted_endorsement_credential();
      migrated = true;
    }
  }

  // Migrate identity data if needed.
  migrated |= MigrateIdentityData();

  if (migrated) {
    EncryptAllEndorsementCredentials();
    LOG(INFO) << "Attestation: Migrated attestation database.";
  }

  // Migrate Rsa PublicKey Format to SubjectPublicKeyInfo
  if (database_pb->credentials().has_legacy_endorsement_public_key()) {
    std::string public_key_info;
    if (GetSubjectPublicKeyInfo(
            database_pb->credentials().endorsement_key_type(),
            database_pb->credentials().legacy_endorsement_public_key(),
            &public_key_info)) {
      database_pb->mutable_credentials()->set_endorsement_public_key(
          public_key_info);
    } else {
      // If the format conversion fails, that means the EK public key is broken
      // somehow, which should not happen. If it does, that means EK data
      // becomes invalid. Clean up all EK metadata to resolve this problem.
      LOG(ERROR) << __func__ << ": Migrate public format fail.";
      database_pb->mutable_credentials()->clear_endorsement_key_type();
      database_pb->mutable_credentials()->clear_endorsement_credential();
    }
    database_pb->mutable_credentials()->clear_legacy_endorsement_public_key();
    migrated |= true;
  }

  return migrated;
}

bool AttestationService::MigrateIdentityData() {
  auto* database_pb = database_->GetMutableProtobuf();
  if (database_pb->identities().size() > 0) {
    // We already migrated identity data.
    return false;
  }

  bool error = false;

  // The identity we're creating will have the next index in identities.
  LOG(INFO) << "Attestation: Migrating existing identity into identity "
            << database_pb->identities().size() << ".";
  CHECK(database_pb->identities().size() == kFirstIdentity);
  AttestationDatabase::Identity* identity_data =
      database_pb->mutable_identities()->Add();
  identity_data->set_features(IDENTITY_FEATURE_ENTERPRISE_ENROLLMENT_ID);
  if (database_pb->has_identity_binding()) {
    identity_data->mutable_identity_binding()->CopyFrom(
        database_pb->identity_binding());
  }
  if (database_pb->has_identity_key()) {
    identity_data->mutable_identity_key()->CopyFrom(
        database_pb->identity_key());
    identity_data->mutable_identity_key()->clear_identity_credential();
    if (database_pb->identity_key().has_identity_credential()) {
      // Create an identity certificate for this identity and the default ACA.
      AttestationDatabase::IdentityCertificate identity_certificate;
      identity_certificate.set_identity(kFirstIdentity);
      identity_certificate.set_aca(DEFAULT_ACA);
      identity_certificate.set_identity_credential(
          database_pb->identity_key().identity_credential());
      auto* map = database_pb->mutable_identity_certificates();
      auto in = map->insert(IdentityCertificateMap::value_type(
          DEFAULT_ACA, identity_certificate));
      if (!in.second) {
        LOG(ERROR) << "Attestation: Could not migrate existing identity.";
        error = true;
      }
    }
    if (database_pb->identity_key().has_enrollment_id()) {
      database_->GetMutableProtobuf()->set_enrollment_id(
          database_pb->identity_key().enrollment_id());
    }
  }

  if (database_pb->has_pcr0_quote()) {
    auto in = identity_data->mutable_pcr_quotes()->insert(
        QuoteMap::value_type(0, database_pb->pcr0_quote()));
    if (!in.second) {
      LOG(ERROR) << "Attestation: Could not migrate existing identity.";
      error = true;
    }
  } else {
    LOG(ERROR) << "Attestation: Missing PCR0 quote in existing database.";
    error = true;
  }
  if (database_pb->has_pcr1_quote()) {
    auto in = identity_data->mutable_pcr_quotes()->insert(
        QuoteMap::value_type(1, database_pb->pcr1_quote()));
    if (!in.second) {
      LOG(ERROR) << "Attestation: Could not migrate existing identity.";
      error = true;
    }
  } else {
    LOG(ERROR) << "Attestation: Missing PCR1 quote in existing database.";
    error = true;
  }

  if (error) {
    database_pb->mutable_identities()->RemoveLast();
    database_pb->mutable_identity_certificates()->erase(DEFAULT_ACA);
  }

  return !error;
}

void AttestationService::ShutdownTask() {
  database_ = nullptr;
  default_database_.reset(nullptr);
  crypto_utility_ = nullptr;
  default_crypto_utility_.reset(nullptr);
  tpm_utility_ = nullptr;
  default_tpm_utility_.reset(nullptr);
}

void AttestationService::GetKeyInfo(const GetKeyInfoRequest& request,
                                    const GetKeyInfoCallback& callback) {
  auto result = std::make_shared<GetKeyInfoReply>();
  base::Closure task = base::Bind(&AttestationService::GetKeyInfoTask,
                                  base::Unretained(this), request, result);
  base::Closure reply =
      base::Bind(&AttestationService::TaskRelayCallback<GetKeyInfoReply>,
                 GetWeakPtr(), callback, result);
  worker_thread_->task_runner()->PostTaskAndReply(FROM_HERE, task, reply);
}

void AttestationService::GetKeyInfoTask(
    const GetKeyInfoRequest& request,
    const std::shared_ptr<GetKeyInfoReply>& result) {
  CertifiedKey key;
  if (!FindKeyByLabel(request.username(), request.key_label(), &key)) {
    result->set_status(STATUS_INVALID_PARAMETER);
    return;
  }
  std::string public_key_info;
  if (!GetSubjectPublicKeyInfo(key.key_type(), key.public_key(),
                               &public_key_info)) {
    LOG(ERROR) << __func__ << ": Bad public key.";
    result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
    return;
  }
  result->set_key_type(key.key_type());
  result->set_key_usage(key.key_usage());
  result->set_public_key(public_key_info);
  result->set_certify_info(key.certified_key_info());
  result->set_certify_info_signature(key.certified_key_proof());
  if (key.has_intermediate_ca_cert()) {
    result->set_certificate(CreatePEMCertificateChain(key));
  } else {
    result->set_certificate(key.certified_key_credential());
  }
  result->set_payload(key.payload());
}

void AttestationService::GetEndorsementInfo(
    const GetEndorsementInfoRequest& request,
    const GetEndorsementInfoCallback& callback) {
  auto result = std::make_shared<GetEndorsementInfoReply>();
  base::Closure task = base::Bind(&AttestationService::GetEndorsementInfoTask,
                                  base::Unretained(this), request, result);
  base::Closure reply = base::Bind(
      &AttestationService::TaskRelayCallback<GetEndorsementInfoReply>,
      GetWeakPtr(), callback, result);
  worker_thread_->task_runner()->PostTaskAndReply(FROM_HERE, task, reply);
}

base::Optional<std::string> AttestationService::GetEndorsementPublicKey()
    const {
  const auto& database_pb = database_->GetProtobuf();
  if (database_pb.has_credentials() &&
      database_pb.credentials().has_endorsement_public_key()) {
    return database_pb.credentials().endorsement_public_key();
  }

  // Try to read the public key directly.
  std::string public_key;
  if (!tpm_utility_->GetEndorsementPublicKey(GetEndorsementKeyType(),
                                             &public_key)) {
    return base::nullopt;
  }
  return public_key;
}

base::Optional<std::string> AttestationService::GetEndorsementCertificate()
    const {
  const auto& database_pb = database_->GetProtobuf();
  if (database_pb.has_credentials() &&
      database_pb.credentials().has_endorsement_credential()) {
    return database_pb.credentials().endorsement_credential();
  }

  // Try to read the certificate directly.
  std::string certificate;
  if (!tpm_utility_->GetEndorsementCertificate(GetEndorsementKeyType(),
                                               &certificate)) {
    return base::nullopt;
  }
  return certificate;
}

void AttestationService::GetEndorsementInfoTask(
    const GetEndorsementInfoRequest& request,
    const std::shared_ptr<GetEndorsementInfoReply>& result) {
  const auto& key_type = GetEndorsementKeyType();

  if (key_type != KEY_TYPE_RSA && key_type != KEY_TYPE_ECC) {
    result->set_status(STATUS_INVALID_PARAMETER);
    return;
  }

  base::Optional<std::string> public_key = GetEndorsementPublicKey();
  if (!public_key.has_value()) {
    LOG(ERROR) << __func__ << ": Endorsement public key not available.";
    result->set_status(STATUS_NOT_AVAILABLE);
    return;
  }

  base::Optional<std::string> certificate = GetEndorsementCertificate();
  if (!certificate.has_value()) {
    LOG(ERROR) << __func__ << ": Endorsement cert not available.";
    result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
    return;
  }

  result->set_ek_public_key(public_key.value());
  result->set_ek_certificate(certificate.value());
  std::string hash = crypto::SHA256HashString(certificate.value());
  result->set_ek_info(
      base::StringPrintf("EK Certificate:\n%s\nHash:\n%s\n",
                         CreatePEMCertificate(certificate.value()).c_str(),
                         base::HexEncode(hash.data(), hash.size()).c_str()));
}

void AttestationService::GetAttestationKeyInfo(
    const GetAttestationKeyInfoRequest& request,
    const GetAttestationKeyInfoCallback& callback) {
  auto result = std::make_shared<GetAttestationKeyInfoReply>();
  base::Closure task =
      base::Bind(&AttestationService::GetAttestationKeyInfoTask,
                 base::Unretained(this), request, result);
  base::Closure reply = base::Bind(
      &AttestationService::TaskRelayCallback<GetAttestationKeyInfoReply>,
      GetWeakPtr(), callback, result);
  worker_thread_->task_runner()->PostTaskAndReply(FROM_HERE, task, reply);
}

void AttestationService::GetAttestationKeyInfoTask(
    const GetAttestationKeyInfoRequest& request,
    const std::shared_ptr<GetAttestationKeyInfoReply>& result) {
  const int identity = kFirstIdentity;
  auto aca_type = request.aca_type();
  auto found = FindIdentityCertificate(identity, aca_type);
  if (found == database_->GetMutableProtobuf()
      ->mutable_identity_certificates()->end()) {
    LOG(ERROR) << __func__ << ": Identity " << identity
               << " is not enrolled for attestation with "
               << GetACAName(aca_type) << ".";
    result->set_status(STATUS_INVALID_PARAMETER);
    return;
  }
  const auto& identity_certificate = found->second;
  if (!IsPreparedForEnrollment() ||identity_certificate.identity() >=
      database_->GetProtobuf().identities().size()) {
    result->set_status(STATUS_NOT_AVAILABLE);
    return;
  }
  const auto& identity_pb = database_->GetProtobuf().identities()
      .Get(identity_certificate.identity());
  if (!identity_pb.has_identity_key()) {
    result->set_status(STATUS_NOT_AVAILABLE);
    return;
  }
  if (identity_pb.identity_key().has_identity_public_key_der()) {
    // TODO(crbug/942487): Use SubjectPublicKeyInfo for identity_public_key_der
    std::string public_key_info;
    if (!GetSubjectPublicKeyInfo(
            identity_pb.identity_key().identity_key_type(),
            identity_pb.identity_key().identity_public_key_der(),
            &public_key_info)) {
      LOG(ERROR) << __func__ << ": Bad public key.";
      result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
      return;
    }
    result->set_public_key(public_key_info);
  }
  if (identity_pb.has_identity_binding() &&
      identity_pb.identity_binding().has_identity_public_key_tpm_format()) {
    result->set_public_key_tpm_format(
        identity_pb.identity_binding().identity_public_key_tpm_format());
  }
  if (identity_certificate.has_identity_credential()) {
    result->set_certificate(identity_certificate.identity_credential());
  }
  if (identity_pb.pcr_quotes().count(0)) {
    *result->mutable_pcr0_quote() = identity_pb.pcr_quotes().at(0);
  }
  if (identity_pb.pcr_quotes().count(1)) {
    *result->mutable_pcr1_quote() = identity_pb.pcr_quotes().at(1);
  }
}

void AttestationService::ActivateAttestationKey(
    const ActivateAttestationKeyRequest& request,
    const ActivateAttestationKeyCallback& callback) {
  auto result = std::make_shared<ActivateAttestationKeyReply>();
  base::Closure task =
      base::Bind(&AttestationService::ActivateAttestationKeyTask,
                 base::Unretained(this), request, result);
  base::Closure reply = base::Bind(
      &AttestationService::TaskRelayCallback<ActivateAttestationKeyReply>,
      GetWeakPtr(), callback, result);
  worker_thread_->task_runner()->PostTaskAndReply(FROM_HERE, task, reply);
}

void AttestationService::ActivateAttestationKeyTask(
    const ActivateAttestationKeyRequest& request,
    const std::shared_ptr<ActivateAttestationKeyReply>& result) {
  if (request.encrypted_certificate().tpm_version() !=
      tpm_utility_->GetVersion()) {
    result->set_status(STATUS_INVALID_PARAMETER);
    LOG(ERROR) << __func__ << ": TPM version mismatch.";
    return;
  }
  std::string certificate;
  if (!ActivateAttestationKeyInternal(
          kFirstIdentity, request.aca_type(), GetEndorsementKeyType(),
          request.encrypted_certificate(), request.save_certificate(),
          &certificate, nullptr /* certificate_index */)) {
    result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
    return;
  }
  result->set_certificate(certificate);
}

void AttestationService::CreateCertifiableKey(
    const CreateCertifiableKeyRequest& request,
    const CreateCertifiableKeyCallback& callback) {
  auto result = std::make_shared<CreateCertifiableKeyReply>();
  base::Closure task = base::Bind(&AttestationService::CreateCertifiableKeyTask,
                                  base::Unretained(this), request, result);
  base::Closure reply = base::Bind(
      &AttestationService::TaskRelayCallback<CreateCertifiableKeyReply>,
      GetWeakPtr(), callback, result);
  worker_thread_->task_runner()->PostTaskAndReply(FROM_HERE, task, reply);
}

void AttestationService::CreateCertifiableKeyTask(
    const CreateCertifiableKeyRequest& request,
    const std::shared_ptr<CreateCertifiableKeyReply>& result) {
  CertifiedKey key;
  if (!CreateKey(request.username(), request.key_label(), request.key_type(),
                 request.key_usage(), &key)) {
    result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
    return;
  }
  std::string public_key_info;
  if (!GetSubjectPublicKeyInfo(key.key_type(), key.public_key(),
                               &public_key_info)) {
    LOG(ERROR) << __func__ << ": Bad public key.";
    result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
    return;
  }
  result->set_public_key(public_key_info);
  result->set_certify_info(key.certified_key_info());
  result->set_certify_info_signature(key.certified_key_proof());
}

void AttestationService::Decrypt(const DecryptRequest& request,
                                 const DecryptCallback& callback) {
  auto result = std::make_shared<DecryptReply>();
  base::Closure task = base::Bind(&AttestationService::DecryptTask,
                                  base::Unretained(this), request, result);
  base::Closure reply =
      base::Bind(&AttestationService::TaskRelayCallback<DecryptReply>,
                 GetWeakPtr(), callback, result);
  worker_thread_->task_runner()->PostTaskAndReply(FROM_HERE, task, reply);
}

void AttestationService::DecryptTask(
    const DecryptRequest& request,
    const std::shared_ptr<DecryptReply>& result) {
  CertifiedKey key;
  if (!FindKeyByLabel(request.username(), request.key_label(), &key)) {
    result->set_status(STATUS_INVALID_PARAMETER);
    return;
  }
  std::string data;
  if (!tpm_utility_->Unbind(key.key_blob(), request.encrypted_data(), &data)) {
    result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
    return;
  }
  result->set_decrypted_data(data);
}

void AttestationService::Sign(const SignRequest& request,
                              const SignCallback& callback) {
  auto result = std::make_shared<SignReply>();
  base::Closure task = base::Bind(&AttestationService::SignTask,
                                  base::Unretained(this), request, result);
  base::Closure reply =
      base::Bind(&AttestationService::TaskRelayCallback<SignReply>,
                 GetWeakPtr(), callback, result);
  worker_thread_->task_runner()->PostTaskAndReply(FROM_HERE, task, reply);
}

void AttestationService::SignTask(const SignRequest& request,
                                  const std::shared_ptr<SignReply>& result) {
  CertifiedKey key;
  if (!FindKeyByLabel(request.username(), request.key_label(), &key)) {
    result->set_status(STATUS_INVALID_PARAMETER);
    return;
  }
  std::string signature;
  if (!tpm_utility_->Sign(key.key_blob(), request.data_to_sign(), &signature)) {
    result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
    return;
  }
  result->set_signature(signature);
}

void AttestationService::RegisterKeyWithChapsToken(
    const RegisterKeyWithChapsTokenRequest& request,
    const RegisterKeyWithChapsTokenCallback& callback) {
  auto result = std::make_shared<RegisterKeyWithChapsTokenReply>();
  base::Closure task =
      base::Bind(&AttestationService::RegisterKeyWithChapsTokenTask,
                 base::Unretained(this), request, result);
  base::Closure reply = base::Bind(
      &AttestationService::TaskRelayCallback<RegisterKeyWithChapsTokenReply>,
      GetWeakPtr(), callback, result);
  worker_thread_->task_runner()->PostTaskAndReply(FROM_HERE, task, reply);
}

void AttestationService::RegisterKeyWithChapsTokenTask(
    const RegisterKeyWithChapsTokenRequest& request,
    const std::shared_ptr<RegisterKeyWithChapsTokenReply>& result) {
  CertifiedKey key;
  if (!FindKeyByLabel(request.username(), request.key_label(), &key)) {
    result->set_status(STATUS_INVALID_PARAMETER);
    return;
  }
  std::string certificate;
  if (request.include_certificates()) {
    certificate = key.certified_key_credential();
  }
  if (!key_store_->Register(request.username(), request.key_label(),
                            key.key_type(), key.key_usage(), key.key_blob(),
                            key.public_key(), certificate)) {
    result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
    return;
  }
  if (request.include_certificates()) {
    if (key.has_intermediate_ca_cert() &&
       !key_store_->RegisterCertificate(request.username(),
                                        key.intermediate_ca_cert())) {
      result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
      return;
    }
    for (int i = 0; i < key.additional_intermediate_ca_cert_size(); ++i) {
      if (!key_store_->RegisterCertificate(
              request.username(), key.additional_intermediate_ca_cert(i))) {
        result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
        return;
      }
    }
  }
  DeleteKey(request.username(), request.key_label());
}

bool AttestationService::IsPreparedForEnrollment() {
  if (!tpm_utility_->IsTpmReady()) {
    return false;
  }
  const auto& database_pb = database_->GetProtobuf();
  // Note that this function only checks for the existence of endorsement
  // credentials, but the identity key, identity key binding and pcr quotes
  // signed by the identity key are also required for enrollment.
  // In normal circumstances, existence of the endorsement credentials implies
  // the existence of the other identity key related pieces, but it is
  // possible for that not to be true, for instance, see crbug.com/899932
  return database_pb.credentials().has_endorsement_credential() ||
         database_pb.credentials().encrypted_endorsement_credentials().size() >
             TEST_ACA;
}

bool AttestationService::IsPreparedForEnrollmentWithACA(ACAType aca_type) {
  const auto& database_pb = database_->GetProtobuf();
  return database_pb.credentials().encrypted_endorsement_credentials().count(
      aca_type);
}
bool AttestationService::IsEnrolled() {
  return IsEnrolledWithACA(DEFAULT_ACA) || IsEnrolledWithACA(TEST_ACA);
}

bool AttestationService::IsEnrolledWithACA(ACAType aca_type) {
  return HasIdentityCertificate(kFirstIdentity, aca_type);
}

AttestationService::IdentityCertificateMap::iterator
AttestationService::FindIdentityCertificate(int identity, ACAType aca_type) {
  auto* database_pb = database_->GetMutableProtobuf();
  auto end = database_pb->mutable_identity_certificates()->end();
  for (auto it = database_pb->mutable_identity_certificates()->begin();
       it != end; ++it) {
    if (it->second.identity() == identity && it->second.aca() == aca_type) {
      return it;
    }
  }
  return end;
}

AttestationDatabase_IdentityCertificate*
AttestationService::FindOrCreateIdentityCertificate(int identity,
                                                    ACAType aca_type,
                                                    int* cert_index) {
  // Find an identity certificate to reuse or create a new one.
  int index;
  auto* database_pb = database_->GetMutableProtobuf();
  auto found = FindIdentityCertificate(identity, aca_type);
  if (found == database_pb->mutable_identity_certificates()->end()) {
    index = identity == kFirstIdentity
        ? aca_type
        : std::max(static_cast<size_t>(kMaxACATypeInternal),
                            database_pb->identity_certificates().size());
    AttestationDatabase::IdentityCertificate new_identity_certificate;
    new_identity_certificate.set_identity(identity);
    new_identity_certificate.set_aca(aca_type);
    auto* map = database_pb->mutable_identity_certificates();
    auto in = map->insert(
        IdentityCertificateMap::value_type(index, new_identity_certificate));
    if (!in.second) {
      LOG(ERROR) << __func__ << ": Failed to create identity certificate "
                 << index << " for identity " << identity << " and "
                 << GetACAName(aca_type) << ".";
      if (cert_index) {
        *cert_index = -1;
      }
      return nullptr;
    }
    found = in.first;
    LOG(INFO) << "Attestation: Creating identity certificate " << index
              << " for identity " << identity << " enrolled with "
              << GetACAName(aca_type);
  } else {
    index = found->first;
  }
  if (cert_index) {
    *cert_index = index;
  }
  return &found->second;
}

bool AttestationService::HasIdentityCertificate(int identity,
                                                ACAType aca_type) {
  return FindIdentityCertificate(identity, aca_type) !=
         database_->GetMutableProtobuf()->
             mutable_identity_certificates()->end();
}

bool AttestationService::CreateEnrollRequestInternal(ACAType aca_type,
    std::string* enroll_request) {
  const int identity = kFirstIdentity;
  if (!IsPreparedForEnrollmentWithACA(aca_type)) {
    LOG(ERROR) << __func__ << ": Enrollment with "
               << GetACAName(aca_type)
               << " is not possible, attestation data does not exist.";
    return false;
  }
  const auto& database_pb = database_->GetProtobuf();
  if (database_pb.identities().size() < identity) {
    LOG(ERROR) << __func__ << ": Enrollment with "
               << GetACAName(aca_type)
               << " is not possible, identity "
               << identity << " does not exist.";
    return false;
  }
  AttestationEnrollmentRequest request_pb;
  request_pb.set_tpm_version(tpm_utility_->GetVersion());
  *request_pb.mutable_encrypted_endorsement_credential() =
      database_pb.credentials().encrypted_endorsement_credentials().at(
          aca_type);
  const AttestationDatabase::Identity& identity_data =
      database_pb.identities().Get(identity);
  request_pb.set_identity_public_key(
      identity_data.identity_binding().identity_public_key_tpm_format());
  *request_pb.mutable_pcr0_quote() = identity_data.pcr_quotes().at(0);
  *request_pb.mutable_pcr1_quote() = identity_data.pcr_quotes().at(1);

  if (identity_data.features() & IDENTITY_FEATURE_ENTERPRISE_ENROLLMENT_ID) {
    std::string enterprise_enrollment_nonce =
        ComputeEnterpriseEnrollmentNonce();
    if (!enterprise_enrollment_nonce.empty()) {
      request_pb.set_enterprise_enrollment_nonce(
          enterprise_enrollment_nonce.data(),
          enterprise_enrollment_nonce.size());
    }

    if (GetEndorsementKeyType() != KEY_TYPE_RSA) {
      // Include an encrypted quote of the RSA pub EK certificate so that
      // an EID can be computed during enrollment.

      auto found = identity_data.nvram_quotes().find(RSA_PUB_EK_CERT);
      if (found == identity_data.nvram_quotes().end()) {
        LOG(ERROR) << __func__
                   << ": Cannot find RSA pub EK certificate quote in identity "
                   << identity << ".";
        return false;
      }

      std::string serialized_quote;
      if (!found->second.SerializeToString(&serialized_quote)) {
        LOG(ERROR) << __func__
                   << ": Failed to serialize RSA pub EK quote protobuf.";
        return false;
      }
      if (!EncryptDataForAttestationCA(aca_type, serialized_quote,
          request_pb.mutable_encrypted_rsa_endorsement_quote())) {
        LOG(ERROR)
            << "Attestation: Failed to encrypt RSA pub EK certificate for "
            << GetACAName(aca_type) << ".";
        return false;
      }
    }
  }

  if (!request_pb.SerializeToString(enroll_request)) {
    LOG(ERROR) << __func__ << ": Failed to serialize protobuf.";
    return false;
  }
  return true;
}

bool AttestationService::FinishEnrollInternal(
    ACAType aca_type,
    const std::string& enroll_response,
    std::string* server_error) {
  const int identity = kFirstIdentity;
  if (!tpm_utility_->IsTpmReady()) {
    LOG(ERROR) << __func__
               << ": Cannot finish enrollment as the TPM is not ready.";
    return false;
  }
  AttestationEnrollmentResponse response_pb;
  if (!response_pb.ParseFromString(enroll_response)) {
    LOG(ERROR) << __func__ << ": Failed to parse response from CA.";
    return false;
  }
  if (response_pb.status() != OK) {
    *server_error = response_pb.detail();
    LogErrorFromCA(__func__, response_pb.detail(),
                   response_pb.extra_details());
    return false;
  }
  if (response_pb.encrypted_identity_credential().tpm_version() !=
      tpm_utility_->GetVersion()) {
    LOG(ERROR) << __func__ << ": TPM version mismatch.";
    return false;
  }
  int certificate_index;
  if (!ActivateAttestationKeyInternal(
          identity, aca_type, GetEndorsementKeyType(),
          response_pb.encrypted_identity_credential(),
          true /* save_certificate */, nullptr /* certificate */,
          &certificate_index)) {
    return false;
  }
  LOG(INFO) << __func__ << ": Enrollment of identity " << identity << " with "
            << GetACAName(aca_type) << " complete. Certificate #"
            << certificate_index << ".";
  return true;
}

bool AttestationService::CreateCertificateRequestInternal(
    ACAType aca_type,
    const std::string& username,
    const CertifiedKey& key,
    CertificateProfile profile,
    const std::string& origin,
    std::string* certificate_request,
    std::string* message_id) {
  if (!tpm_utility_->IsTpmReady()) {
    return false;
  }
  if (!IsEnrolledWithACA(aca_type)) {
    LOG(ERROR) << __func__ << ": Device is not enrolled for attestation with "
               << GetACAName(aca_type) << ".";
    return false;
  }
  auto found = FindIdentityCertificate(kFirstIdentity, aca_type);
  if (found == database_->GetMutableProtobuf()
      ->mutable_identity_certificates()->end()) {
    LOG(ERROR) << __func__ << ": Identity " << kFirstIdentity
               << " is not enrolled for attestation with "
               << GetACAName(aca_type) << ".";
    return false;
  }
  const auto& identity_certificate = found->second;
  if (!crypto_utility_->GetRandom(kNonceSize, message_id)) {
    LOG(ERROR) << __func__ << ": GetRandom(message_id) failed.";
    return false;
  }
  AttestationCertificateRequest request_pb;
  request_pb.set_tpm_version(tpm_utility_->GetVersion());
  request_pb.set_message_id(*message_id);
  request_pb.set_identity_credential(
      identity_certificate.identity_credential());
  request_pb.set_profile(profile);

#if USE_TPM2

  if (profile == ENTERPRISE_ENROLLMENT_CERTIFICATE) {
    const int identity = identity_certificate.identity();
    const AttestationDatabase::Identity& identity_data =
        database_->GetProtobuf().identities().Get(identity);
    // Copy NVRAM quotes to include in an enrollment certificate from the
    // identity if possible.
    std::set<NVRAMQuoteType> not_in_identity;
    for (int i = 0; i < arraysize(kNvramQuoteTypeForEnrollmentCertificate);
         ++i) {
      const auto quote_type = kNvramQuoteTypeForEnrollmentCertificate[i];
      const auto found = identity_data.nvram_quotes().find(quote_type);
      if (found != identity_data.nvram_quotes().cend()) {
        (*request_pb.mutable_nvram_quotes())[quote_type] = found->second;
      } else {
        not_in_identity.insert(quote_type);
      }
    }
    // Data that is supposed to be in the identiy but is missing won't be
    // quoted now, as we want to drive everything from the identity.
    for (int i = 0; i < arraysize(kNvramQuoteTypeInIdentityData); ++i) {
      const auto quote_type = kNvramQuoteTypeInIdentityData[i];
      if (not_in_identity.erase(quote_type)) {
        LOG(WARNING) << "Could not find "
                     << kNvramIndexData[nvram_quote_type_to_index_data_[
                            quote_type]].quote_name
                     << " quote in identity " << identity
                     << " to provide in enrollment cert request.";
      }
    }
    // Quote the other data now.
    for (auto it = not_in_identity.cbegin(), end = not_in_identity.cend();
         it != end; ++it) {
      Quote quote;
      if (QuoteNvramData(*it, identity_data.identity_key(), &quote)) {
        (*request_pb.mutable_nvram_quotes())[*it] = quote;
      } else {
        LOG(WARNING) << "Could not provide "
                     << kNvramIndexData[nvram_quote_type_to_index_data_[*it]]
                           .quote_name
                     << " quote in enrollment cert request.";
      }
    }
  }

#endif

  if (!origin.empty() &&
      (profile == CONTENT_PROTECTION_CERTIFICATE_WITH_STABLE_ID)) {
    request_pb.set_origin(origin);
    request_pb.set_temporal_index(ChooseTemporalIndex(username, origin));
  }
  request_pb.set_certified_public_key(key.public_key_tpm_format());
  request_pb.set_certified_key_info(key.certified_key_info());
  request_pb.set_certified_key_proof(key.certified_key_proof());
  if (!request_pb.SerializeToString(certificate_request)) {
    LOG(ERROR) << __func__ << ": Failed to serialize protobuf.";
    return false;
  }
  return true;
}

bool AttestationService::FinishCertificateRequestInternal(
    const std::string& certificate_response,
    const std::string& username,
    const std::string& key_label,
    const std::string& message_id,
    CertifiedKey* key,
    std::string* certificate_chain,
    std::string* server_error) {
  if (!tpm_utility_->IsTpmReady()) {
    return false;
  }
  AttestationCertificateResponse response_pb;
  if (!response_pb.ParseFromString(certificate_response)) {
    LOG(ERROR) << __func__ << ": Failed to parse response from Attestation CA.";
    return false;
  }
  if (response_pb.status() != OK) {
    *server_error = response_pb.detail();
    LogErrorFromCA(__func__, response_pb.detail(),
                   response_pb.extra_details());
    return false;
  }
  if (message_id != response_pb.message_id()) {
    LOG(ERROR) << __func__ << ": Message ID mismatch.";
    return false;
  }
  return PopulateAndStoreCertifiedKey(response_pb, username, key_label,
                                      key, certificate_chain);
}

bool AttestationService::PopulateAndStoreCertifiedKey(
    const AttestationCertificateResponse& response_pb,
    const std::string& username,
    const std::string& key_label,
    CertifiedKey* key,
    std::string* certificate_chain) {
  // Finish populating the CertifiedKey protobuf and store it.
  key->set_key_name(key_label);
  key->set_certified_key_credential(response_pb.certified_key_credential());
  key->set_intermediate_ca_cert(response_pb.intermediate_ca_cert());
  key->mutable_additional_intermediate_ca_cert()->MergeFrom(
      response_pb.additional_intermediate_ca_cert());
  if (!SaveKey(username, key_label, *key)) {
    return false;
  }
  LOG(INFO) << "Attestation: Certified key credential received and stored.";
  *certificate_chain = CreatePEMCertificateChain(*key);
  return true;
}

bool AttestationService::FindKeyByLabel(const std::string& username,
                                        const std::string& key_label,
                                        CertifiedKey* key) {
  if (!username.empty()) {
    std::string key_data;
    if (!key_store_->Read(username, key_label, &key_data)) {
      LOG(INFO) << "Key not found: " << key_label;
      return false;
    }
    if (key && !key->ParseFromString(key_data)) {
      LOG(ERROR) << "Failed to parse key: " << key_label;
      return false;
    }
    return true;
  }
  auto database_pb = database_->GetProtobuf();
  for (int i = 0; i < database_pb.device_keys_size(); ++i) {
    if (database_pb.device_keys(i).key_name() == key_label) {
      *key = database_pb.device_keys(i);
      return true;
    }
  }
  LOG(INFO) << "Key not found: " << key_label;
  return false;
}

bool AttestationService::CreateKey(const std::string& username,
                                   const std::string& key_label,
                                   KeyType key_type,
                                   KeyUsage key_usage,
                                   CertifiedKey* key) {
  std::string nonce;
  if (!crypto_utility_->GetRandom(kNonceSize, &nonce)) {
    LOG(ERROR) << __func__ << ": GetRandom(nonce) failed.";
    return false;
  }
  std::string key_blob;
  std::string public_key;
  std::string public_key_tpm_format;
  std::string key_info;
  std::string proof;
  auto database_pb = database_->GetProtobuf();
  if (!tpm_utility_->CreateCertifiedKey(
          key_type, key_usage, database_pb.identity_key().identity_key_blob(),
          nonce, &key_blob, &public_key, &public_key_tpm_format, &key_info,
          &proof)) {
    return false;
  }
  key->set_key_blob(key_blob);
  key->set_public_key(public_key);
  key->set_key_name(key_label);
  key->set_public_key_tpm_format(public_key_tpm_format);
  key->set_certified_key_info(key_info);
  key->set_certified_key_proof(proof);
  key->set_key_type(key_type);
  key->set_key_usage(key_usage);
  return SaveKey(username, key_label, *key);
}

bool AttestationService::SaveKey(const std::string& username,
                                 const std::string& key_label,
                                 const CertifiedKey& key) {
  if (!username.empty()) {
    std::string key_data;
    if (!key.SerializeToString(&key_data)) {
      LOG(ERROR) << __func__ << ": Failed to serialize protobuf.";
      return false;
    }
    if (!key_store_->Write(username, key_label, key_data)) {
      LOG(ERROR) << __func__ << ": Failed to store certified key for user.";
      return false;
    }
  } else {
    if (!AddDeviceKey(key_label, key)) {
      LOG(ERROR) << __func__ << ": Failed to store certified key for device.";
      return false;
    }
  }
  return true;
}

void AttestationService::DeleteKey(const std::string& username,
                                   const std::string& key_label) {
  if (!username.empty()) {
    key_store_->Delete(username, key_label);
  } else {
    RemoveDeviceKey(key_label);
  }
}

bool AttestationService::DeleteKeysByPrefix(
    const std::string& username,
    const std::string& key_prefix) {
  if (!username.empty()) {
    return key_store_->DeleteByPrefix(username, key_prefix);
  }
  return RemoveDeviceKeysByPrefix(key_prefix);
}

bool AttestationService::AddDeviceKey(const std::string& key_label,
                                      const CertifiedKey& key) {
  // If a key by this name already exists, reuse the field.
  auto* database_pb = database_->GetMutableProtobuf();
  bool found = false;
  for (int i = 0; i < database_pb->device_keys_size(); ++i) {
    if (database_pb->device_keys(i).key_name() == key_label) {
      found = true;
      *database_pb->mutable_device_keys(i) = key;
      break;
    }
  }
  if (!found)
    *database_pb->add_device_keys() = key;
  return database_->SaveChanges();
}

void AttestationService::RemoveDeviceKey(const std::string& key_label) {
  auto* database_pb = database_->GetMutableProtobuf();
  bool found = false;
  for (int i = 0; i < database_pb->device_keys_size(); ++i) {
    if (database_pb->device_keys(i).key_name() == key_label) {
      found = true;
      int last = database_pb->device_keys_size() - 1;
      if (i < last) {
        database_pb->mutable_device_keys()->SwapElements(i, last);
      }
      database_pb->mutable_device_keys()->RemoveLast();
      break;
    }
  }
  if (found) {
    if (!database_->SaveChanges()) {
      LOG(WARNING) << __func__ << ": Failed to persist key deletion.";
    }
  }
}

bool AttestationService::RemoveDeviceKeysByPrefix(
    const std::string& key_prefix) {
  // Manipulate the device keys protobuf field.  Linear time strategy is to swap
  // all elements we want to keep to the front and then truncate.
  auto* device_keys = database_->GetMutableProtobuf()->mutable_device_keys();
  int next_keep_index = 0;
  for (int i = 0; i < device_keys->size(); ++i) {
    if (device_keys->Get(i).key_name().find(key_prefix) != 0) {
      // Prefix doesn't match -> keep.
      if (i != next_keep_index)
        device_keys->SwapElements(next_keep_index, i);
      ++next_keep_index;
    }
  }
  // If no matching keys, do nothing and return success.
  if (next_keep_index == device_keys->size()) {
    return true;
  }
  while (next_keep_index < device_keys->size()) {
    device_keys->RemoveLast();
  }
  return database_->SaveChanges();
}

std::string AttestationService::CreatePEMCertificateChain(
    const CertifiedKey& key) {
  if (key.certified_key_credential().empty()) {
    LOG(WARNING) << "Certificate is empty.";
    return std::string();
  }
  std::string pem = CreatePEMCertificate(key.certified_key_credential());
  if (!key.intermediate_ca_cert().empty()) {
    pem += "\n";
    pem += CreatePEMCertificate(key.intermediate_ca_cert());
  }
  for (int i = 0; i < key.additional_intermediate_ca_cert_size(); ++i) {
    pem += "\n";
    pem += CreatePEMCertificate(key.additional_intermediate_ca_cert(i));
  }
  return pem;
}

std::string AttestationService::CreatePEMCertificate(
    const std::string& certificate) {
  const char kBeginCertificate[] = "-----BEGIN CERTIFICATE-----\n";
  const char kEndCertificate[] = "-----END CERTIFICATE-----";

  std::string pem = kBeginCertificate;
  pem += brillo::data_encoding::Base64EncodeWrapLines(certificate);
  pem += kEndCertificate;
  return pem;
}

int AttestationService::ChooseTemporalIndex(const std::string& user,
                                            const std::string& origin) {
  std::string user_hash = crypto::SHA256HashString(user);
  std::string origin_hash = crypto::SHA256HashString(origin);
  int histogram[kNumTemporalValues] = {};
  auto database_pb = database_->GetProtobuf();
  for (int i = 0; i < database_pb.temporal_index_record_size(); ++i) {
    const AttestationDatabase::TemporalIndexRecord& record =
        database_pb.temporal_index_record(i);
    // Ignore out-of-range index values.
    if (record.temporal_index() < 0 ||
        record.temporal_index() >= kNumTemporalValues)
      continue;
    if (record.origin_hash() == origin_hash) {
      if (record.user_hash() == user_hash) {
        // We've previously chosen this index for this user, reuse it.
        return record.temporal_index();
      } else {
        // We've previously chosen this index for another user.
        ++histogram[record.temporal_index()];
      }
    }
  }
  int least_used_index = 0;
  for (int i = 1; i < kNumTemporalValues; ++i) {
    if (histogram[i] < histogram[least_used_index])
      least_used_index = i;
  }
  if (histogram[least_used_index] > 0) {
    LOG(WARNING) << "Unique origin-specific identifiers have been exhausted.";
  }
  // Record our choice for later reference.
  AttestationDatabase::TemporalIndexRecord* new_record =
      database_pb.add_temporal_index_record();
  new_record->set_origin_hash(origin_hash);
  new_record->set_user_hash(user_hash);
  new_record->set_temporal_index(least_used_index);
  database_->SaveChanges();
  return least_used_index;
}

bool AttestationService::GetSubjectPublicKeyInfo(
    KeyType key_type,
    const std::string& public_key,
    std::string* public_key_info) const {
  if (key_type == KEY_TYPE_RSA) {
    return crypto_utility_->GetRSASubjectPublicKeyInfo(public_key,
                                                       public_key_info);
  } else if (key_type == KEY_TYPE_ECC) {
    // Do nothing, since we always store SubjectPublicKeyInfo in |public_key|
    // field and will pass it this utility
    *public_key_info = public_key;
    return true;
  } else {
    LOG(ERROR) << __func__ << ": key_type " << key_type << " isn't supported.";
    return false;
  }
}

void AttestationService::PrepareForEnrollment() {
  if (IsPreparedForEnrollment()) {
    return;
  }
  if (!tpm_utility_->IsTpmReady()) {
    // Try again later.
    worker_thread_->task_runner()->PostDelayedTask(
        FROM_HERE, base::Bind(&AttestationService::PrepareForEnrollment,
                              base::Unretained(this)),
        base::TimeDelta::FromSeconds(3));
    return;
  }
  base::TimeTicks start = base::TimeTicks::Now();
  LOG(INFO) << "Attestation: Preparing for enrollment...";

  KeyType key_type = GetEndorsementKeyType();

  // Gather information about the endorsement key.
  std::string ek_public_key;
  if (!tpm_utility_->GetEndorsementPublicKey(key_type, &ek_public_key)) {
    LOG(ERROR) << __func__ << ": Failed to get EK public key with key_type "
               << key_type;
    return;
  }
  LOG(INFO) << "GetEndorsementPublicKey done. (from start: "
            << (base::TimeTicks::Now() - start).InMilliseconds() << "ms.)";

  std::string ek_certificate;
  if (!tpm_utility_->GetEndorsementCertificate(key_type, &ek_certificate)) {
    LOG(ERROR) << __func__ << ": Failed to get " << GetKeyTypeName(key_type)
               << " EK certificate.";
    return;
  }
  LOG(INFO) << "GetEndorsementCertificate done. (from start: "
            << (base::TimeTicks::Now() - start).InMilliseconds() << "ms.)";

  // Create a new AIK and PCR quotes for the first identity with default
  // identity features.
  if (CreateIdentity(default_identity_features_) < 0) {
    return;
  }
  LOG(INFO) << "CreateIdentity done. (from start: "
            << (base::TimeTicks::Now() - start).InMilliseconds() << "ms.)";

  // Store all this in the attestation database.
  auto* database_pb = database_->GetMutableProtobuf();
  TPMCredentials* credentials_pb = database_pb->mutable_credentials();
  credentials_pb->set_endorsement_key_type(key_type);
  credentials_pb->set_endorsement_public_key(ek_public_key);
  credentials_pb->set_endorsement_credential(ek_certificate);

  // Encrypt the endorsement credential for all the ACAs we know of.
  EncryptAllEndorsementCredentials();

  if (!database_->SaveChanges()) {
    LOG(ERROR) << "Attestation: Failed to write database.";
    return;
  }

  // Ignore errors when removing dependency. If failed this time, will be
  // re-attempted on next boot.
  tpm_utility_->RemoveOwnerDependency();

  base::TimeDelta delta = (base::TimeTicks::Now() - start);
  LOG(INFO) << "Attestation: Prepared successfully (" << delta.InMilliseconds()
            << "ms) with " << GetKeyTypeName(key_type) << " EK.";
}

int AttestationService::CreateIdentity(int identity_features) {
  // The identity we're creating will have the next index in identities.
  auto* database_pb = database_->GetMutableProtobuf();
  const int identity = database_pb->identities().size();
  KeyType identity_key_type = GetAttestationIdentityKeyType();
  LOG(INFO) << "Attestation: Creating identity " << identity << " with "
            << GetIdentityFeaturesString(identity_features) << " and "
            << GetKeyTypeName(identity_key_type) << " AIK.";
  AttestationDatabase::Identity new_identity_pb;

  new_identity_pb.set_features(identity_features);
  if (identity_features & IDENTITY_FEATURE_ENTERPRISE_ENROLLMENT_ID) {
    auto* identity_key = new_identity_pb.mutable_identity_key();
    identity_key->set_enrollment_id(database_pb->enrollment_id());
  }
  if (!tpm_utility_->CreateIdentity(identity_key_type, &new_identity_pb)) {
    LOG(ERROR) << __func__ << " failed to make a new identity.";
    return -1;
  }
  std::string identity_key_blob_for_quote =
      new_identity_pb.identity_key().identity_key_blob();

  // Quote PCRs and store them in the identity. These quotes are intended to
  // be valid for the lifetime of the identity key so they do not need
  // external data. This only works when firmware ensures that these PCRs
  // will not change unless the TPM owner is cleared.
  auto* pcr_quote_map = new_identity_pb.mutable_pcr_quotes();

  for (int pcr = 0; pcr <= kLastPcrToQuote; ++pcr) {
    std::string quoted_pcr_value;
    std::string quoted_data;
    std::string quote;
    if (tpm_utility_->QuotePCR(pcr, identity_key_blob_for_quote,
                               &quoted_pcr_value, &quoted_data, &quote)) {
      Quote quote_pb;
      quote_pb.set_quote(quote);
      quote_pb.set_quoted_data(quoted_data);
      quote_pb.set_quoted_pcr_value(quoted_pcr_value);
      switch (pcr) {
        case 1:
          quote_pb.set_pcr_source_hint(hwid_);
          break;
      }
      auto in = pcr_quote_map->insert(QuoteMap::value_type(pcr, quote_pb));
      if (!in.second) {
        LOG(ERROR) << "Attestation: Failed to store PCR" << pcr
                   << " quote for identity " << identity << ".";
        return -1;
      }
    } else {
      LOG(ERROR) << "Attestation: Failed to generate quote for PCR" << pcr
                 << ".";
      return -1;
    }
  }

#if USE_TPM2

  // Certify device-specific NV data and insert them in the identity when
  // we can certify them. This is an almost identical process to the PCR
  // quotes above.

  for (int i = 0; i < arraysize(kNvramQuoteTypeInIdentityData); ++i) {
    if (!InsertCertifiedNvramData(kNvramQuoteTypeInIdentityData[i],
                                  false /* must_be_present */,
                                  &new_identity_pb)) {
      return -1;
    }
  }

  // Certify the RSA EK cert only when we are using non-RSA EK. In this case, we
  // don't provide the RSA EK cert which originally is used for calculating the
  // Enrollment ID.
  if ((identity_features & IDENTITY_FEATURE_ENTERPRISE_ENROLLMENT_ID) &&
      GetEndorsementKeyType() != KEY_TYPE_RSA) {
    if (!InsertCertifiedNvramData(RSA_PUB_EK_CERT,
                                  true /* must_be_present */,
                                  &new_identity_pb)) {
      return -1;
    }
  }

#endif
  database_pb->add_identities()->CopyFrom(new_identity_pb);
  // Return the index of the newly created identity.
  return database_pb->identities().size() - 1;
}

bool AttestationService::QuoteNvramData(
    NVRAMQuoteType quote_type,
    const IdentityKey& identity_key,
    Quote* quote) {
#if USE_TPM2
  const int id = nvram_quote_type_to_index_data_[quote_type];
  const char* quote_name = kNvramIndexData[id].quote_name;
  const uint32_t nv_index = kNvramIndexData[id].nv_index;
  uint16_t nv_size = kNvramIndexData[id].nv_size;

  if (!nv_size) {
    uint16_t nv_data_size = 0;
    if (tpm_utility_->GetNVDataSize(nv_index, &nv_data_size)) {
      nv_size = nv_data_size;
    } else {
      LOG(ERROR) << "Attestation: Failed to obtain data about the "
                << quote_name << ".";
    }
  }

  if (nv_size) {
    auto identity_key_blob = identity_key.identity_key_blob();
    std::string certified_value;
    std::string signature;

    if (tpm_utility_->CertifyNV(nv_index, nv_size, identity_key_blob,
                                &certified_value, &signature)) {
      quote->set_quote(signature);
      quote->set_quoted_data(certified_value);
      return true;
    } else {
      LOG(WARNING) << "Attestation: Failed to certify " << quote_name
                   << " NV data of size " << nv_size << " at index "
                   << std::hex << std::showbase << nv_index << ".";
    }
  }
#else
  LOG(ERROR) << __func__ << ": Should not be called for TPM 1.2 devices.";
#endif

  return false;
}

bool AttestationService::InsertCertifiedNvramData(
    NVRAMQuoteType quote_type,
    bool must_be_present,
    AttestationDatabase::Identity* identity) {
#if USE_TPM2
  Quote quote;
  if (!QuoteNvramData(quote_type, identity->identity_key(), &quote)) {
    return !must_be_present;
  }

  auto* nv_quote_map = identity->mutable_nvram_quotes();
  auto in_bid = nv_quote_map->insert(QuoteMap::value_type(quote_type, quote));
  if (in_bid.second) {
    return true;
  } else {
    LOG(ERROR) << "Attestation: Failed to store "
               << kNvramIndexData[nvram_quote_type_to_index_data_[quote_type]]
                      .quote_name
               << " quote for identity " << identity << ".";
  }
#else
  LOG(ERROR) << __func__ << ": Should not be called for TPM 1.2 devices.";
#endif

  return false;
}

int AttestationService::GetIdentitiesCount() const {
  return database_->GetProtobuf().identities().size();
}

int AttestationService::GetIdentityFeatures(int identity) const {
  return database_->GetProtobuf().identities().Get(identity).features();
}

AttestationService::IdentityCertificateMap
AttestationService::GetIdentityCertificateMap() const {
  return database_->GetProtobuf().identity_certificates();
}

bool AttestationService::EncryptAllEndorsementCredentials() {
  auto* database_pb = database_->GetMutableProtobuf();
  base::Optional<std::string> ek_certificate = GetEndorsementCertificate();
  if (!ek_certificate.has_value()) {
    LOG(ERROR) << "Attestation: Failed to obtain endorsement certificate.";
    return false;
  }

  TPMCredentials* credentials_pb = database_pb->mutable_credentials();
  for (int aca = kDefaultACA; aca < kMaxACATypeInternal; ++aca) {
    if (credentials_pb->encrypted_endorsement_credentials().count(aca)) {
      continue;
    }
    ACAType aca_type = GetACAType(static_cast<ACATypeInternal>(aca));
    LOG(INFO) << "Attestation: Encrypting endorsement credential for "
              << GetACAName(aca_type) << ".";
    if (!EncryptDataForAttestationCA(
            aca_type, ek_certificate.value(),
            &(*credentials_pb
                   ->mutable_encrypted_endorsement_credentials())[aca])) {
      LOG(ERROR) << "Attestation: Failed to encrypt EK certificate for "
                   << GetACAName(static_cast<ACAType>(aca)) << ".";
      return false;
    }
  }
  return true;
}

bool AttestationService::EncryptDataForAttestationCA(
    ACAType aca_type,
    const std::string& data,
    EncryptedData* encrypted_data) {
  std::string key;
  std::string key_id;
  switch (aca_type) {
    case DEFAULT_ACA:
      key = kDefaultACAPublicKey;
      key_id = std::string(kDefaultACAPublicKeyID,
                        arraysize(kDefaultACAPublicKeyID) - 1);
      break;
    case TEST_ACA:
      key = kTestACAPublicKey;
      key_id = std::string(kTestACAPublicKeyID,
                        arraysize(kTestACAPublicKeyID) - 1);
      break;
    default:
      NOTREACHED();
  }
  if (!crypto_utility_->EncryptDataForGoogle(
       data, key, key_id, encrypted_data)) {
      return false;
  }
  return true;
}

bool AttestationService::ActivateAttestationKeyInternal(
    int identity,
    ACAType aca_type,
    KeyType ek_key_type,
    const EncryptedIdentityCredential& encrypted_certificate,
    bool save_certificate,
    std::string* certificate,
    int* certificate_index) {
  const auto& database_pb = database_->GetProtobuf();
  if (database_pb.identities().size() < identity) {
    LOG(ERROR) << __func__ << ": Enrollment is not possible, identity "
               << identity << " does not exist.";
    return false;
  }
  const auto& identity_data = database_pb.identities().Get(identity);
  std::string certificate_local;
  if (encrypted_certificate.tpm_version() == TPM_1_2) {
    // TPM 1.2 style activate.
    if (!tpm_utility_->ActivateIdentity(
            identity_data.identity_key().identity_key_blob(),
            encrypted_certificate.asym_ca_contents(),
            encrypted_certificate.sym_ca_attestation(),
            &certificate_local)) {
      LOG(ERROR) << __func__ << ": Failed to activate identity "
                 << identity << ".";
      return false;
    }
  } else {
    // TPM 2.0 style activate.
    std::string credential;
    if (!tpm_utility_->ActivateIdentityForTpm2(
            ek_key_type, identity_data.identity_key().identity_key_blob(),
            encrypted_certificate.encrypted_seed(),
            encrypted_certificate.credential_mac(),
            encrypted_certificate.wrapped_certificate().wrapped_key(),
            &credential)) {
      LOG(ERROR) << __func__ << ": Failed to activate identity "
                 << identity << ".";
      return false;
    }
    if (!crypto_utility_->DecryptIdentityCertificateForTpm2(
            credential, encrypted_certificate.wrapped_certificate(),
            &certificate_local)) {
      LOG(ERROR) << __func__ << ": Failed to decrypt certificate for identity "
                 << identity << ".";
      return false;
    }
  }
  if (save_certificate) {
    int index;
    AttestationDatabase_IdentityCertificate* identity_certificate =
        FindOrCreateIdentityCertificate(identity, aca_type, &index);
    if (!identity_certificate) {
      LOG(ERROR) << __func__ << ": Failed to find or create an identity"
                 << " certificate for identity " << identity << " with "
                 << GetACAName(aca_type) << ".";
      return false;
    }
    // Set the credential obtained when activating the identity with the
    // response.
    identity_certificate->set_identity_credential(certificate_local);
    if (!database_->SaveChanges()) {
      LOG(ERROR) << __func__ << ": Failed to persist database changes.";
      return false;
    }
    if (certificate_index) {
      *certificate_index = index;
    }
  }
  if (certificate) {
    *certificate = certificate_local;
  }
  return true;
}

void AttestationService::GetEnrollmentPreparations(
    const GetEnrollmentPreparationsRequest& request,
    const GetEnrollmentPreparationsCallback& callback) {
  auto result = std::make_shared<GetEnrollmentPreparationsReply>();
  base::Closure task =
      base::Bind(&AttestationService::GetEnrollmentPreparationsTask,
                 base::Unretained(this), request, result);
  base::Closure reply = base::Bind(
      &AttestationService::TaskRelayCallback<GetEnrollmentPreparationsReply>,
      GetWeakPtr(), callback, result);
  worker_thread_->task_runner()->PostTaskAndReply(FROM_HERE, task, reply);
}

void AttestationService::GetEnrollmentPreparationsTask(
    const GetEnrollmentPreparationsRequest& request,
    const std::shared_ptr<GetEnrollmentPreparationsReply>& result) {
  for (int aca = kDefaultACA; aca < kMaxACATypeInternal; ++aca) {
    ACAType aca_type = GetACAType(static_cast<ACATypeInternal>(aca));
    if (!request.has_aca_type() || aca_type == request.aca_type()) {
      (*result->mutable_enrollment_preparations())[aca_type] =
            IsPreparedForEnrollmentWithACA(aca_type);
    }
  }
}

void AttestationService::GetStatus(
    const GetStatusRequest& request,
    const GetStatusCallback& callback) {
  auto result = std::make_shared<GetStatusReply>();
  base::Closure task =
      base::Bind(&AttestationService::GetStatusTask,
                 base::Unretained(this), request, result);
  base::Closure reply = base::Bind(
      &AttestationService::TaskRelayCallback<GetStatusReply>,
      GetWeakPtr(), callback, result);
  worker_thread_->task_runner()->PostTaskAndReply(FROM_HERE, task, reply);
}

bool AttestationService::IsVerifiedMode() const {
  if (!tpm_utility_->IsTpmReady()) {
    VLOG(2) << __func__ << ": Tpm is not ready.";
    return false;
  }
  std::string pcr_value;
  if (!tpm_utility_->ReadPCR(0, &pcr_value)) {
    LOG(WARNING) << __func__ << ": Failed to read PCR0.";
    return false;
  }
  return (pcr_value == GetPCRValueForMode(kVerifiedBootMode));
}

void AttestationService::GetStatusTask(
    const GetStatusRequest& request,
    const std::shared_ptr<GetStatusReply>& result) {
  result->set_prepared_for_enrollment(IsPreparedForEnrollment());
  result->set_enrolled(IsEnrolled());
  for (int i = 0, count = GetIdentitiesCount(); i < count; ++i) {
    GetStatusReply::Identity* identity = result->mutable_identities()->Add();
    identity->set_features(GetIdentityFeatures(i));
  }
  AttestationService::IdentityCertificateMap map = GetIdentityCertificateMap();
  for (auto it = map.cbegin(), end = map.cend(); it != end; ++it) {
    GetStatusReply::IdentityCertificate identity_certificate;
    identity_certificate.set_identity(it->second.identity());
    identity_certificate.set_aca(it->second.aca());
    result->mutable_identity_certificates()->insert(
        google::protobuf::Map<int, GetStatusReply::IdentityCertificate>::
            value_type(it->first, identity_certificate));
  }
  for (int aca = kDefaultACA; aca < kMaxACATypeInternal; ++aca) {
    ACAType aca_type = GetACAType(static_cast<ACATypeInternal>(aca));
    (*result->mutable_enrollment_preparations())[aca_type] =
        IsPreparedForEnrollmentWithACA(aca_type);
  }
  if (request.extended_status()) {
    result->set_verified_boot(IsVerifiedMode());
  }
}

void AttestationService::Verify(
    const VerifyRequest& request,
    const VerifyCallback& callback) {
  auto result = std::make_shared<VerifyReply>();
  base::Closure task =
      base::Bind(&AttestationService::VerifyTask,
                 base::Unretained(this), request, result);
  base::Closure reply = base::Bind(
      &AttestationService::TaskRelayCallback<VerifyReply>,
      GetWeakPtr(), callback, result);
  worker_thread_->task_runner()->PostTaskAndReply(FROM_HERE, task, reply);
}

bool AttestationService::VerifyIdentityBinding(
    const IdentityBinding& binding) {
  if (tpm_utility_->GetVersion() == TPM_1_2) {
    // Reconstruct and hash a serialized TPM_IDENTITY_CONTENTS structure.
    const std::string header("\x01\x01\x00\x00\x00\x00\x00\x79", 8);
    std::string digest =
        base::SHA1HashString(binding.identity_label() +
                             binding.pca_public_key());
    std::string identity_public_key_info;
    if (!GetSubjectPublicKeyInfo(KEY_TYPE_RSA,
                                 binding.identity_public_key_der(),
                                 &identity_public_key_info)) {
      LOG(ERROR) << __func__ << ": Failed to get identity public key info.";
      return false;
    }
    if (!crypto_utility_->VerifySignature(
            crypto_utility_->DefaultDigestAlgoForSingature(),
            identity_public_key_info,
            header + digest + binding.identity_public_key_tpm_format(),
            binding.identity_binding())) {
      LOG(ERROR) << __func__
                 << ": Failed to verify identity binding signature.";
      return false;
    }
  } else if (tpm_utility_->GetVersion() == TPM_2_0) {
    VLOG(1) << __func__ << ": Nothing to do for TPM 2.0.";
  } else {
    LOG(ERROR) << __func__ << ": Unsupported TPM version.";
    return false;
  }
  return true;
}

bool AttestationService::VerifyQuoteSignature(
    const std::string& aik_public_key_info,
    const Quote& quote,
    uint32_t pcr_index) {
  if (!crypto_utility_->VerifySignature(
          crypto_utility_->DefaultDigestAlgoForSingature(), aik_public_key_info,
          quote.quoted_data(), quote.quote())) {
    LOG(ERROR) << __func__ << ": Signature mismatch.";
    return false;
  }
  if (!tpm_utility_->IsQuoteForPCR(quote.quoted_pcr_value(),
                                   quote.quoted_data(), quote.quote(),
                                   pcr_index)) {
    LOG(ERROR) << __func__ << ": Invalid quote.";
    return false;
  }
  return true;
}

std::string AttestationService::GetPCRValueForMode(const char* mode) const {
  std::string mode_str(mode, 3);
  std::string mode_digest = base::SHA1HashString(mode_str);
  std::string pcr_value;
  if (tpm_utility_->GetVersion() == TPM_1_2) {
    // Use SHA-1 digests for TPM 1.2.
    std::string initial(base::kSHA1Length, 0);
    pcr_value = base::SHA1HashString(initial + mode_digest);
  } else if (tpm_utility_->GetVersion() == TPM_2_0) {
    // Use SHA-256 digests for TPM 2.0.
    std::string initial(crypto::kSHA256Length, 0);
    mode_digest.resize(crypto::kSHA256Length);
    pcr_value = crypto::SHA256HashString(initial + mode_digest);
  } else {
    LOG(ERROR) << __func__ << ": Unsupported TPM version.";
  }
  return pcr_value;
}

bool AttestationService::VerifyPCR0Quote(
    const std::string& aik_public_key_info,
    const Quote& pcr0_quote) {
  if (!VerifyQuoteSignature(aik_public_key_info, pcr0_quote, 0)) {
    return false;
  }

  // Check if the PCR0 value represents a known mode.
  for (size_t i = 0; i < arraysize(kKnownBootModes); ++i) {
    std::string pcr_value = GetPCRValueForMode(kKnownBootModes[i]);
    if (pcr0_quote.quoted_pcr_value() == pcr_value) {
      LOG(INFO) << "PCR0: " << GetDescriptionForMode(kKnownBootModes[i]);
      return true;
    }
  }
  LOG(WARNING) << "PCR0 value not recognized.";
  return true;
}

bool AttestationService::VerifyPCR1Quote(
    const std::string& aik_public_key_info,
    const Quote& pcr1_quote) {
  if (!VerifyQuoteSignature(aik_public_key_info, pcr1_quote, 1)) {
    return false;
  }

  // Check that the source hint is correctly populated.
  if (hwid_ != pcr1_quote.pcr_source_hint()) {
    LOG(ERROR) << "PCR1 source hint does not match HWID: " << hwid_;
    return false;
  }

  LOG(INFO) << "PCR1 verified as " << hwid_;
  return true;
}

bool AttestationService::GetCertifiedKeyDigest(
    const std::string& public_key_info,
    const std::string& public_key_tpm_format,
    std::string* key_digest) {
  if (tpm_utility_->GetVersion() == TPM_1_2) {
    return crypto_utility_->GetKeyDigest(public_key_info, key_digest);
  } else if (tpm_utility_->GetVersion() == TPM_2_0) {
    // TPM_ALG_SHA256 = 0x000B, here in big-endian order.
    std::string prefix("\x00\x0B", 2);
    key_digest->assign(
        prefix + crypto::SHA256HashString(public_key_tpm_format));
    return true;
  }
  LOG(ERROR) << __func__ << ": Unsupported TPM version.";
  return false;
}

bool AttestationService::VerifyCertifiedKey(
    const std::string& aik_public_key_info,
    const std::string& public_key_info,
    const std::string& public_key_tpm_format,
    const std::string& key_info,
    const std::string& proof) {
  if (!crypto_utility_->VerifySignature(
          crypto_utility_->DefaultDigestAlgoForSingature(), aik_public_key_info,
          key_info, proof)) {
    LOG(ERROR) << __func__ << ": Bad key signature.";
    return false;
  }
  std::string key_digest;
  if (!GetCertifiedKeyDigest(public_key_info, public_key_tpm_format,
                             &key_digest)) {
    LOG(ERROR) << __func__ << ": Failed to get key digest.";
    return false;
  }
  if (key_info.find(key_digest) == std::string::npos) {
    LOG(ERROR) << __func__ << ": Public key mismatch.";
    return false;
  }
  return true;
}

bool AttestationService::VerifyCertifiedKeyGeneration(
    const std::string& aik_key_blob,
    const std::string& aik_public_key_info) {
  std::string nonce;
  if (!crypto_utility_->GetRandom(kNonceSize, &nonce)) {
    LOG(ERROR) << __func__ << ": GetRandom(nonce) failed.";
    return false;
  }
  std::vector<KeyType> key_type_candidates;
  if (tpm_utility_->GetVersion() == TPM_2_0) {
    key_type_candidates = {KEY_TYPE_RSA, KEY_TYPE_ECC};
  } else {
    key_type_candidates = {KEY_TYPE_RSA};
  }

  for (KeyType key_type : key_type_candidates) {
    std::string key_blob;
    std::string public_key_der;
    std::string public_key_tpm_format;
    std::string key_info;
    std::string proof;
    if (!tpm_utility_->CreateCertifiedKey(
            key_type, KEY_USAGE_SIGN, aik_key_blob, nonce, &key_blob,
            &public_key_der, &public_key_tpm_format, &key_info, &proof)) {
      LOG(ERROR) << __func__
                 << ": Failed to create certified key for key_type: "
                 << key_type;
      return false;
    }
    std::string public_key_info;
    if (!GetSubjectPublicKeyInfo(key_type, public_key_der, &public_key_info)) {
      LOG(ERROR) << __func__ << ": Failed to get public key info for key_type: "
                 << key_type;
      return false;
    }
    if (!VerifyCertifiedKey(aik_public_key_info, public_key_info,
                            public_key_tpm_format, key_info, proof)) {
      LOG(ERROR) << __func__
                 << ": Bad certified key for key_type: " << key_type;
      return false;
    }
  }
  return true;
}

bool AttestationService::VerifyActivateIdentity(
    const std::string& aik_public_key_tpm_format) {
  std::string rsa_ek_public_key;
  if (!tpm_utility_->GetEndorsementPublicKey(KEY_TYPE_RSA,
                                             &rsa_ek_public_key)) {
    LOG(ERROR) << __func__
               << ": Can't get RSA EK public key for VerifyActivateIdentity.";
    return false;
  }
  std::string test_credential = "test credential";
  EncryptedIdentityCredential encrypted_credential;
  if (!crypto_utility_->EncryptIdentityCredential(
          tpm_utility_->GetVersion(), test_credential, rsa_ek_public_key,
          aik_public_key_tpm_format, &encrypted_credential)) {
    LOG(ERROR) << __func__ << ": Failed to encrypt identity credential";
    return false;
  }
  if (!ActivateAttestationKeyInternal(kFirstIdentity, DEFAULT_ACA, KEY_TYPE_RSA,
                                      encrypted_credential, false, nullptr,
                                      nullptr)) {
    LOG(ERROR) << __func__ << ": Failed to activate identity";
    return false;
  }
  return true;
}

void AttestationService::VerifyTask(
    const VerifyRequest& request,
    const std::shared_ptr<VerifyReply>& result) {
  result->set_verified(false);

  base::Optional<std::string> ek_public_key = GetEndorsementPublicKey();
  if (!ek_public_key.has_value()) {
    LOG(ERROR) << __func__ << ": Endorsement key not available.";
    return;
  }

  base::Optional<std::string> ek_cert = GetEndorsementCertificate();
  if (!ek_cert.has_value()) {
    LOG(ERROR) << __func__ << ": Endorsement cert not available.";
    return;
  }

  std::string issuer;
  if (!crypto_utility_->GetCertificateIssuerName(ek_cert.value(), &issuer)) {
    LOG(ERROR) << __func__ << ": Failed to get certificate issuer.";
    return;
  }
  std::string ca_public_key;
  if (!GetAuthorityPublicKey(issuer, request.cros_core(), &ca_public_key)) {
    LOG(ERROR) << __func__ << ": Failed to get CA public key.";
    return;
  }
  if (!crypto_utility_->VerifyCertificate(ek_cert.value(), ca_public_key)) {
    LOG(WARNING) << __func__ << ": Bad endorsement credential.";
    return;
  }

  // Verify that the given public key matches the public key in the credential.
  // Note: Do not use any openssl functions that attempt to decode the public
  // key. These will fail because openssl does not recognize the OAEP key type.
  // Note2: GetCertificatePublicKey will return SubjectPublicKeyInfo.
  // TODO(crbug/942487): remove Note2 comments after migration
  std::string cert_public_key_info;
  if (!crypto_utility_->GetCertificatePublicKey(ek_cert.value(),
                                                &cert_public_key_info)) {
    LOG(ERROR) << __func__ << ": Failed to get certificate public key.";
    return;
  }
  if (cert_public_key_info != ek_public_key.value()) {
    LOG(ERROR) << __func__ << ": Bad certificate public key.";
    return;
  }

  // All done if we only needed to verify EK. Otherwise, continue with full
  // verification.
  if (request.ek_only()) {
    result->set_verified(true);
    return;
  }

  auto database_pb = database_->GetProtobuf();
  const auto& identity_data = database_pb.identities().Get(kFirstIdentity);
  std::string identity_public_key_info;
  if (!GetSubjectPublicKeyInfo(
          identity_data.identity_key().identity_key_type(),
          identity_data.identity_key().identity_public_key_der(),
          &identity_public_key_info)) {
    LOG(ERROR) << __func__ << ": Failed to get identity public key info.";
    return;
  }
  if (!VerifyIdentityBinding(identity_data.identity_binding())) {
    LOG(ERROR) << __func__ << ": Bad identity binding.";
    return;
  }
  if (!VerifyPCR0Quote(identity_public_key_info,
                       identity_data.pcr_quotes().at(0))) {
    LOG(ERROR) << __func__ << ": Bad PCR0 quote.";
    return;
  }
  if (!VerifyPCR1Quote(identity_public_key_info,
                       identity_data.pcr_quotes().at(1))) {
    // Don't fail because many devices don't use PCR1.
    LOG(WARNING) << __func__ << ": Bad PCR1 quote.";
  }
  if (!VerifyCertifiedKeyGeneration(
           identity_data.identity_key().identity_key_blob(),
           identity_public_key_info)) {
    LOG(ERROR) << __func__ << ": Failed to verify certified key generation.";
    return;
  }

  // Originally, we use VerifyActivateIdentity to test ActivateIdentity TPM
  // command works in TPM 1.2, but we don't really need it for TPM 2.0. Because
  // we have more complete tests in Cr50, we are planning to obsolete the tests
  // in the service daemon. Moreover, supporting this would require simulating
  // the entire attestation flow but pointless. We decide to not to support this
  // test for ECC EK.
  if (!VerifyActivateIdentity(
          identity_data.identity_binding().identity_public_key_tpm_format())) {
    LOG(ERROR) << __func__ << ": Failed to verify identity activation.";
    return;
  }
  LOG(INFO) << "Attestation: Verified OK.";
  result->set_verified(true);
}

void AttestationService::CreateEnrollRequest(
    const CreateEnrollRequestRequest& request,
    const CreateEnrollRequestCallback& callback) {
  auto result = std::make_shared<CreateEnrollRequestReply>();
  base::Closure task =
      base::Bind(&AttestationService::CreateEnrollRequestTask,
                 base::Unretained(this), request, result);
  base::Closure reply = base::Bind(
      &AttestationService::TaskRelayCallback<CreateEnrollRequestReply>,
      GetWeakPtr(), callback, result);
  worker_thread_->task_runner()->PostTaskAndReply(FROM_HERE, task, reply);
}

void AttestationService::CreateEnrollRequestTask(
    const CreateEnrollRequestRequest& request,
    const std::shared_ptr<CreateEnrollRequestReply>& result) {
  if (!CreateEnrollRequestInternal(request.aca_type(),
                                   result->mutable_pca_request())) {
    result->clear_pca_request();
    result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
  }
}

void AttestationService::FinishEnroll(
    const FinishEnrollRequest& request,
    const FinishEnrollCallback& callback) {
  auto result = std::make_shared<FinishEnrollReply>();
  base::Closure task =
      base::Bind(&AttestationService::FinishEnrollTask,
                 base::Unretained(this), request, result);
  base::Closure reply = base::Bind(
      &AttestationService::TaskRelayCallback<FinishEnrollReply>,
      GetWeakPtr(), callback, result);
  worker_thread_->task_runner()->PostTaskAndReply(FROM_HERE, task, reply);
}

void AttestationService::FinishEnrollTask(
    const FinishEnrollRequest& request,
    const std::shared_ptr<FinishEnrollReply>& result) {
  std::string server_error;
  if (!FinishEnrollInternal(request.aca_type(), request.pca_response(),
                            &server_error)) {
    if (server_error.empty()) {
      result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
    } else {
      result->set_status(STATUS_REQUEST_DENIED_BY_CA);
    }
  }
}

void AttestationService::CreateCertificateRequest(
    const CreateCertificateRequestRequest& request,
    const CreateCertificateRequestCallback& callback) {
  auto result = std::make_shared<CreateCertificateRequestReply>();
  base::Closure task =
      base::Bind(&AttestationService::CreateCertificateRequestTask,
                 base::Unretained(this), request, result);
  base::Closure reply = base::Bind(
      &AttestationService::TaskRelayCallback<CreateCertificateRequestReply>,
      GetWeakPtr(), callback, result);
  worker_thread_->task_runner()->PostTaskAndReply(FROM_HERE, task, reply);
}

void AttestationService::CreateCertificateRequestTask(
    const CreateCertificateRequestRequest& request,
    const std::shared_ptr<CreateCertificateRequestReply>& result) {
  const int identity = kFirstIdentity;
  auto database_pb = database_->GetProtobuf();
  if (database_pb.identities().size() < identity) {
    LOG(ERROR) << __func__ << ": Cannot create a certificate request, identity "
               << identity << " does not exist.";
    return;
  }
  std::string key_label;
  if (!crypto_utility_->GetRandom(kNonceSize, &key_label)) {
    LOG(ERROR) << __func__ << ": GetRandom(message_id) failed.";
    result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
    return;
  }
  std::string nonce;
  if (!crypto_utility_->GetRandom(kNonceSize, &nonce)) {
    LOG(ERROR) << __func__ << ": GetRandom(nonce) failed.";
    result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
    return;
  }
  KeyType key_type = request.key_type();
  std::string key_blob;
  std::string public_key_der;
  std::string public_key_tpm_format;
  std::string key_info;
  std::string proof;
  CertifiedKey key;
  const auto& identity_data = database_pb.identities().Get(identity);
  if (!tpm_utility_->CreateCertifiedKey(
          key_type, KEY_USAGE_SIGN,
          identity_data.identity_key().identity_key_blob(), nonce, &key_blob,
          &public_key_der, &public_key_tpm_format, &key_info, &proof)) {
    LOG(ERROR) << __func__ << ": Failed to create a key.";
    result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
    return;
  }
  key.set_key_blob(key_blob);
  key.set_public_key(public_key_der);
  key.set_key_name(key_label);
  key.set_public_key_tpm_format(public_key_tpm_format);
  key.set_certified_key_info(key_info);
  key.set_certified_key_proof(proof);
  key.set_key_type(key_type);
  key.set_key_usage(KEY_USAGE_SIGN);
  std::string message_id;
  if (!CreateCertificateRequestInternal(request.aca_type(),
                                        request.username(), key,
                                        request.certificate_profile(),
                                        request.request_origin(),
                                        result->mutable_pca_request(),
                                        &message_id)) {
    result->clear_pca_request();
    result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
    return;
  }
  std::string serialized_key;
  if (!key.SerializeToString(&serialized_key)) {
    LOG(ERROR) << __func__ << ": Failed to serialize key protobuf.";
    result->clear_pca_request();
    result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
    return;
  }
  pending_cert_requests_[message_id] = serialized_key;
}

void AttestationService::FinishCertificateRequest(
    const FinishCertificateRequestRequest& request,
    const FinishCertificateRequestCallback& callback) {
  auto result = std::make_shared<FinishCertificateRequestReply>();
  base::Closure task =
      base::Bind(&AttestationService::FinishCertificateRequestTask,
                 base::Unretained(this), request, result);
  base::Closure reply = base::Bind(
      &AttestationService::TaskRelayCallback<FinishCertificateRequestReply>,
      GetWeakPtr(), callback, result);
  worker_thread_->task_runner()->PostTaskAndReply(FROM_HERE, task, reply);
}

void AttestationService::FinishCertificateRequestTask(
    const FinishCertificateRequestRequest& request,
    const std::shared_ptr<FinishCertificateRequestReply>& result) {
  AttestationCertificateResponse response_pb;
  if (!response_pb.ParseFromString(request.pca_response())) {
    LOG(ERROR) << __func__ << ": Failed to parse response from Attestation CA.";
    result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
    return;
  }
  CertRequestMap::iterator iter = pending_cert_requests_.find(
      response_pb.message_id());
  if (iter == pending_cert_requests_.end()) {
    LOG(ERROR) << __func__ << ": Pending request not found.";
    result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
    return;
  }
  if (response_pb.status() != OK) {
    LogErrorFromCA(__func__, response_pb.detail(),
                   response_pb.extra_details());
    pending_cert_requests_.erase(iter);
    result->set_status(STATUS_REQUEST_DENIED_BY_CA);
    return;
  }
  CertifiedKey key;
  if (!key.ParseFromArray(iter->second.data(),
                          iter->second.size())) {
    LOG(ERROR) << __func__ << ": Failed to parse pending request key.";
    pending_cert_requests_.erase(iter);
    result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
    return;
  }
  pending_cert_requests_.erase(iter);
  if (!PopulateAndStoreCertifiedKey(response_pb, request.username(),
                                    request.key_label(), &key,
                                    result->mutable_certificate())) {
    result->clear_certificate();
    result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
    return;
  }
}

const char *AttestationService::GetEnterpriseSigningHexKey(VAType va_type)
    const {
  return va_type == TEST_VA ? kTestEnterpriseSigningPublicKey :
      kDefaultEnterpriseSigningPublicKey;
}

const char *AttestationService::GetEnterpriseEncryptionHexKey(VAType va_type)
    const {
  return va_type == TEST_VA ? kTestEnterpriseEncryptionPublicKey :
      kDefaultEnterpriseEncryptionPublicKey;
}

std::string AttestationService::GetEnterpriseEncryptionPublicKeyID(
    VAType va_type) const {
  return std::string(va_type == TEST_VA ?
      kTestEnterpriseEncryptionPublicKeyID :
      kDefaultEnterpriseEncryptionPublicKeyID,
      arraysize(va_type == TEST_VA ?
          kTestEnterpriseEncryptionPublicKeyID :
          kDefaultEnterpriseEncryptionPublicKeyID) - 1);
}

bool AttestationService::ValidateEnterpriseChallenge(
    VAType va_type,
    const SignedData& signed_challenge) {
  const char kExpectedChallengePrefix[] = "EnterpriseKeyChallenge";
  // VA server always use SHA256 as digest algorithm for signature.
  if (!crypto_utility_->VerifySignatureUsingHexKey(
          NID_sha256, GetEnterpriseSigningHexKey(va_type),
          signed_challenge.data(), signed_challenge.signature())) {
    LOG(ERROR) << __func__ << ": Failed to verify challenge signature.";
    return false;
  }
  Challenge challenge;
  if (!challenge.ParseFromString(signed_challenge.data())) {
    LOG(ERROR) << __func__ << ": Failed to parse challenge protobuf.";
    return false;
  }
  if (challenge.prefix() != kExpectedChallengePrefix) {
    LOG(ERROR) << __func__ << ": Unexpected challenge prefix.";
    return false;
  }
  return true;
}

bool AttestationService::EncryptEnterpriseKeyInfo(
    VAType va_type,
    const KeyInfo& key_info,
    EncryptedData* encrypted_data) {
  std::string serialized;
  if (!key_info.SerializeToString(&serialized)) {
    LOG(ERROR) << "Failed to serialize key info.";
    return false;
  }
  return crypto_utility_->EncryptDataForGoogle(
             serialized, GetEnterpriseEncryptionHexKey(va_type),
             GetEnterpriseEncryptionPublicKeyID(va_type), encrypted_data);
}

void AttestationService::SignEnterpriseChallenge(
    const SignEnterpriseChallengeRequest& request,
    const SignEnterpriseChallengeCallback& callback) {
  auto result = std::make_shared<SignEnterpriseChallengeReply>();
  base::Closure task =
      base::Bind(&AttestationService::SignEnterpriseChallengeTask,
                 base::Unretained(this), request, result);
  base::Closure reply = base::Bind(
      &AttestationService::TaskRelayCallback<SignEnterpriseChallengeReply>,
      GetWeakPtr(), callback, result);
  worker_thread_->task_runner()->PostTaskAndReply(FROM_HERE, task, reply);
}

void AttestationService::SignEnterpriseChallengeTask(
    const SignEnterpriseChallengeRequest& request,
    const std::shared_ptr<SignEnterpriseChallengeReply>& result) {
  CertifiedKey key;
  if (!FindKeyByLabel(request.username(), request.key_label(), &key)) {
    result->set_status(STATUS_INVALID_PARAMETER);
    return;
  }

  // Validate that the challenge is coming from the expected source.
  SignedData signed_challenge;
  if (!signed_challenge.ParseFromString(request.challenge())) {
    LOG(ERROR) << __func__ << ": Failed to parse signed challenge.";
    result->set_status(STATUS_INVALID_PARAMETER);
    return;
  }
  if (!ValidateEnterpriseChallenge(request.va_type(), signed_challenge)) {
    LOG(ERROR) << __func__ << ": Invalid challenge.";
    result->set_status(STATUS_INVALID_PARAMETER);
    return;
  }
  // Add a nonce to ensure this service cannot be used to sign arbitrary data.
  std::string nonce;
  if (!crypto_utility_->GetRandom(kChallengeSignatureNonceSize, &nonce)) {
    LOG(ERROR) << __func__ << ": Failed to generate nonce.";
    result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
    return;
  }

  bool is_user_specific = request.has_username();
  KeyInfo key_info;
  // EUK -> Enterprise User Key
  // EMK -> Enterprise Machine Key
  key_info.set_key_type(is_user_specific ? EUK : EMK);
  key_info.set_domain(request.domain());
  key_info.set_device_id(request.device_id());

  base::Optional<CertifiedKey> key_for_certificate_and_spkac;
  if (is_user_specific) {
    // Always include the EUK certificate if an EUK is being challenged.
    // Note that if including SPKAC has been requested when challenging an EUK,
    // the SPKAC will also be created for the EUK. In other words,
    // |key_name_for_spkac| is currently ignored for EUKs.
    key_for_certificate_and_spkac = key;
  } else if (request.include_signed_public_key() &&
             !request.key_name_for_spkac().empty()) {
    // If a specific key name for SPKAC has been requested when challenging an
    // EMK, include the certificate for that key.
    CertifiedKey key_for_spkac;
    if (!FindKeyByLabel(std::string() /* username */,
                        request.key_name_for_spkac(),
                        &key_for_spkac)) {
      result->set_status(STATUS_INVALID_PARAMETER);
      return;
    }
    key_for_certificate_and_spkac = key_for_spkac;
  }
  if (key_for_certificate_and_spkac) {
    key_info.set_certificate(
        CreatePEMCertificateChain(key_for_certificate_and_spkac.value()));
    if (request.include_signed_public_key()) {
      std::string spkac;
      if (!crypto_utility_->CreateSPKAC(
              key_for_certificate_and_spkac.value().key_blob(),
              key_for_certificate_and_spkac.value().public_key(),
              &spkac)) {
        LOG(ERROR) << __func__ << ": Failed to create signed public key.";
        result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
        return;
      }
      key_info.set_signed_public_key_and_challenge(spkac);
    }
  }

  ChallengeResponse response_pb;
  *response_pb.mutable_challenge() = signed_challenge;
  response_pb.set_nonce(nonce);
  if (!EncryptEnterpriseKeyInfo(request.va_type(),
                                key_info,
                                response_pb.mutable_encrypted_key_info())) {
    LOG(ERROR) << __func__ << ": Failed to encrypt KeyInfo.";
    result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
    return;
  }

  // Serialize and sign the response protobuf.
  std::string serialized;
  if (!response_pb.SerializeToString(&serialized)) {
    LOG(ERROR) << __func__ << ": Failed to serialize response protobuf.";
    result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
    return;
  }
  if (!SignChallengeData(key, serialized,
                         result->mutable_challenge_response())) {
    result->clear_challenge_response();
    result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
    return;
  }
}

void AttestationService::SignSimpleChallenge(
    const SignSimpleChallengeRequest& request,
    const SignSimpleChallengeCallback& callback) {
  auto result = std::make_shared<SignSimpleChallengeReply>();
  base::Closure task =
      base::Bind(&AttestationService::SignSimpleChallengeTask,
                 base::Unretained(this), request, result);
  base::Closure reply = base::Bind(
      &AttestationService::TaskRelayCallback<SignSimpleChallengeReply>,
      GetWeakPtr(), callback, result);
  worker_thread_->task_runner()->PostTaskAndReply(FROM_HERE, task, reply);
}

void AttestationService::SignSimpleChallengeTask(
    const SignSimpleChallengeRequest& request,
    const std::shared_ptr<SignSimpleChallengeReply>& result) {
  CertifiedKey key;
  if (!FindKeyByLabel(request.username(), request.key_label(), &key)) {
    result->set_status(STATUS_INVALID_PARAMETER);
    return;
  }
  // Add a nonce to ensure this service cannot be used to sign arbitrary data.
  std::string nonce;
  if (!crypto_utility_->GetRandom(kChallengeSignatureNonceSize, &nonce)) {
    LOG(ERROR) << __func__ << ": Failed to generate nonce.";
    result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
    return;
  }
  if (!SignChallengeData(key, request.challenge() + nonce,
                         result->mutable_challenge_response())) {
    result->clear_challenge_response();
    result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
    return;
  }
}

bool AttestationService::SignChallengeData(const CertifiedKey& key,
                                           const std::string& data_to_sign,
                                           std::string* response) {
  std::string signature;
  if (!tpm_utility_->Sign(key.key_blob(), data_to_sign, &signature)) {
    LOG(ERROR) << __func__ << ": Failed to sign data.";
    return false;
  }
  SignedData signed_data;
  signed_data.set_data(data_to_sign);
  signed_data.set_signature(signature);
  if (!signed_data.SerializeToString(response)) {
    LOG(ERROR) << __func__ << ": Failed to serialize signed data.";
    return false;
  }
  return true;
}

void AttestationService::SetKeyPayload(
    const SetKeyPayloadRequest& request,
    const SetKeyPayloadCallback& callback) {
  auto result = std::make_shared<SetKeyPayloadReply>();
  base::Closure task =
      base::Bind(&AttestationService::SetKeyPayloadTask,
                 base::Unretained(this), request, result);
  base::Closure reply = base::Bind(
      &AttestationService::TaskRelayCallback<SetKeyPayloadReply>,
      GetWeakPtr(), callback, result);
  worker_thread_->task_runner()->PostTaskAndReply(FROM_HERE, task, reply);
}

void AttestationService::SetKeyPayloadTask(
    const SetKeyPayloadRequest& request,
    const std::shared_ptr<SetKeyPayloadReply>& result) {
  CertifiedKey key;
  if (!FindKeyByLabel(request.username(), request.key_label(), &key)) {
    result->set_status(STATUS_INVALID_PARAMETER);
    return;
  }
  key.set_payload(request.payload());
  if (!SaveKey(request.username(), request.key_label(), key)) {
    result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
    return;
  }
}

void AttestationService::DeleteKeys(
    const DeleteKeysRequest& request,
    const DeleteKeysCallback& callback) {
  auto result = std::make_shared<DeleteKeysReply>();
  base::Closure task =
      base::Bind(&AttestationService::DeleteKeysTask,
                 base::Unretained(this), request, result);
  base::Closure reply = base::Bind(
      &AttestationService::TaskRelayCallback<DeleteKeysReply>,
      GetWeakPtr(), callback, result);
  worker_thread_->task_runner()->PostTaskAndReply(FROM_HERE, task, reply);
}

void AttestationService::DeleteKeysTask(
    const DeleteKeysRequest& request,
    const std::shared_ptr<DeleteKeysReply>& result) {
  if (!DeleteKeysByPrefix(request.username(), request.key_prefix())) {
    LOG(ERROR) << __func__ << ": Failed to delete keys with prefix: "
               << request.key_prefix();
    result->set_status(STATUS_UNEXPECTED_DEVICE_ERROR);
    return;
  }
}

void AttestationService::ResetIdentity(
    const ResetIdentityRequest& request,
    const ResetIdentityCallback& callback) {
  auto result = std::make_shared<ResetIdentityReply>();
  base::Closure task =
      base::Bind(&AttestationService::ResetIdentityTask,
                 base::Unretained(this), request, result);
  base::Closure reply = base::Bind(
      &AttestationService::TaskRelayCallback<ResetIdentityReply>,
      GetWeakPtr(), callback, result);
  worker_thread_->task_runner()->PostTaskAndReply(FROM_HERE, task, reply);
}

void AttestationService::ResetIdentityTask(
    const ResetIdentityRequest& request,
    const std::shared_ptr<ResetIdentityReply>& result) {
  LOG(ERROR) << __func__ << ": Not implemented.";
  result->set_status(STATUS_NOT_SUPPORTED);
}

void AttestationService::SetSystemSalt(
    const SetSystemSaltRequest& request,
    const SetSystemSaltCallback& callback) {
  system_salt_.assign(request.system_salt());
  brillo::cryptohome::home::SetSystemSalt(&system_salt_);
  SetSystemSaltReply result;
  callback.Run(result);
}

void AttestationService::GetEnrollmentId(
    const GetEnrollmentIdRequest& request,
    const GetEnrollmentIdCallback& callback) {
  auto result = std::make_shared<GetEnrollmentIdReply>();
  base::Closure task =
      base::Bind(&AttestationService::GetEnrollmentIdTask,
                 base::Unretained(this), request, result);

  base::Closure reply = base::Bind(
      &AttestationService::TaskRelayCallback<GetEnrollmentIdReply>,
      GetWeakPtr(), callback, result);
  worker_thread_->task_runner()->PostTaskAndReply(FROM_HERE, task, reply);
}

void AttestationService::GetEnrollmentIdTask(
    const GetEnrollmentIdRequest& request,
    const std::shared_ptr<GetEnrollmentIdReply>& result) {
  std::string enrollment_id;
  if (request.ignore_cache()) {
    enrollment_id = ComputeEnterpriseEnrollmentId();
  } else {
    const auto& database_pb = database_->GetProtobuf();
    if (database_pb.has_enrollment_id()) {
      enrollment_id = std::string(database_pb.enrollment_id());
    } else {
      enrollment_id = ComputeEnterpriseEnrollmentId();
      if (!enrollment_id.empty()) {
        database_->GetMutableProtobuf()->set_enrollment_id(enrollment_id);
        database_->SaveChanges();
      }
    }
  }
  if (enrollment_id.empty()) {
    result->set_status(STATUS_NOT_AVAILABLE);
  }
  result->set_enrollment_id(enrollment_id);
}

void AttestationService::GetCertifiedNvIndex(
    const GetCertifiedNvIndexRequest& request,
    const GetCertifiedNvIndexCallback& callback) {
  auto result = std::make_shared<GetCertifiedNvIndexReply>();
  base::Closure task = base::Bind(&AttestationService::GetCertifiedNvIndexTask,
                                  base::Unretained(this), request, result);

  base::Closure reply = base::Bind(
      &AttestationService::TaskRelayCallback<GetCertifiedNvIndexReply>,
      GetWeakPtr(), callback, result);
  worker_thread_->task_runner()->PostTaskAndReply(FROM_HERE, task, reply);
}

void AttestationService::GetCertifiedNvIndexTask(
    const GetCertifiedNvIndexRequest& request,
    const std::shared_ptr<GetCertifiedNvIndexReply>& result) {
  result->set_status(STATUS_NOT_AVAILABLE);

  auto database_pb = database_->GetProtobuf();
  if (database_pb.identities().size() < kFirstIdentity) {
    LOG(ERROR) << __func__ << ": Cannot certify NV index, identity "
               << kFirstIdentity << " does not exist.";
    return;
  }

  const auto& identity_pb = database_pb.identities(kFirstIdentity);
  if (!identity_pb.has_identity_key()) {
    LOG(ERROR) << __func__ << ": Identify key missing";
    return;
  }

  auto cert_it = FindIdentityCertificate(kFirstIdentity, DEFAULT_ACA);
  if (cert_it ==
      database_->GetMutableProtobuf()->mutable_identity_certificates()->end()) {
    LOG(ERROR) << __func__ << ": Identity " << kFirstIdentity
               << " is not enrolled for attestation with "
               << GetACAName(DEFAULT_ACA) << ".";
    return;
  }

  std::string certified_value;
  std::string signature;

  if (!tpm_utility_->CertifyNV(request.nv_index(), request.nv_size(),
                               identity_pb.identity_key().identity_key_blob(),
                               &certified_value, &signature)) {
    LOG(WARNING) << "Attestation: Failed to certify NV data of size "
                 << request.nv_size() << " at index " << std::hex
                 << std::showbase << request.nv_index() << ".";
    result->set_status(STATUS_INVALID_PARAMETER);
    return;
  }

  const auto& identity_certificate = cert_it->second;
  if (identity_certificate.has_identity_credential()) {
    result->set_identity_certificate(
        identity_certificate.identity_credential());
  }

  result->set_certified_data(certified_value);
  result->set_signature(signature);
  result->set_status(STATUS_SUCCESS);
}

std::string AttestationService::ComputeEnterpriseEnrollmentNonce() {
  if (!abe_data_ || abe_data_->empty()) {
    // If there was no device secret we cannot compute the DEN.
    // We do not want to fail attestation for those devices.
    return "";
  }

  std::string data(abe_data_->char_data(), abe_data_->size());
  std::string key(kAttestationBasedEnterpriseEnrollmentContextName);
  return crypto_utility_->HmacSha256(key, data);
}

std::string AttestationService::ComputeEnterpriseEnrollmentId() {
  std::string den = ComputeEnterpriseEnrollmentNonce();
  if (den.empty())
    return "";

  std::string ekm;
  if (!tpm_utility_->GetEndorsementPublicKeyModulus(KEY_TYPE_RSA, &ekm))
    return "";

  // Compute the EID based on den and ekm.
  return crypto_utility_->HmacSha256(den, ekm);
}

KeyType AttestationService::GetEndorsementKeyType() const {
  // If some EK information already exists in the database, we need to keep the
  // key type consistent.
  const auto& database_pb = database_->GetProtobuf();
  if (database_pb.credentials().has_endorsement_public_key() ||
      database_pb.credentials().has_endorsement_credential()) {
    // We use the default value of key_type for backward compatibility, no need
    // to check if endorsement_key_type is set.
    return database_pb.credentials().endorsement_key_type();
  }

  // We didn't generate any data yet. Use the suggested key type.
  return tpm_utility_->GetVersion() == TPM_2_0 ? KEY_TYPE_ECC : KEY_TYPE_RSA;
}

KeyType AttestationService::GetAttestationIdentityKeyType() const {
  return tpm_utility_->GetVersion() == TPM_2_0 ? KEY_TYPE_ECC : KEY_TYPE_RSA;
}

base::WeakPtr<AttestationService> AttestationService::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

ACAType AttestationService::GetACAType(ACATypeInternal aca_type_internal) {
    switch (aca_type_internal) {
      case kDefaultACA:
        return DEFAULT_ACA;
      case kTestACA:
        return TEST_ACA;
      default:
        return DEFAULT_ACA;
    }
}

}  // namespace attestation
