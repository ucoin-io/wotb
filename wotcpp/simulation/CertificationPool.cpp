#include <Duniter.h>
#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <chrono>
#include <iomanip>

#include "simulation/CertificationPool.h"
#include "log.h"

namespace libsimu {

  using namespace std;
  using namespace libsimu;
  std::random_device rd;
  std::mt19937 eng(rd());

  CertificationPool::CertificationPool(int dureeSimulationEnBlocks, double xPercent, uint32_t stepMax, uint32_t minNew, double maxNewPercent, uint32_t sigMoy, uint32_t sigStock, uint32_t sigQty, uint32_t sigPeriod) {
    certs = vector<vector<Certification*>>(10000000);
    liens = vector<vector<Certification*>>(10000000);
    counter = 0;
    NOMBRE_DE_BLOCKS_DE_SIMULATION = dureeSimulationEnBlocks;
    X_PERCENT = xPercent;
    STEPMAX = stepMax;
    MIN_NEW = minNew;
    MAX_NEW_PERCENT = maxNewPercent;
    SIG_STOCK = sigStock;
    SIG_QTY = sigQty;
    SIG_PERIOD = sigPeriod;
    SIG_VALIDITY = sigStock * SIG_PERIOD;
    COMMUNAUTE_INITIALE = sigQty + 1;
    expirationsDeLiens = vector<vector<Certification*>>(NOMBRE_DE_BLOCKS_DE_SIMULATION + 1);
  }


  CertificationPool::~CertificationPool() {
  }

  void CertificationPool::createNew(Identity* from, Identity* to, uint32_t date) {
//      Log2() << "Nouvelle certif de UID " << from->uid << " -> " << to->uid;
    Certification* cert = new Certification();
    cert->dateOfIssuance = date;
    cert->emetteur = from;
    cert->receveur = to;
    cert->uniqueNumber = counter++;
    Lien link;
    link.first = from->uid;
    link.second = to->uid;
    cert->link = link;
    certs[to->uid].push_back(cert);
    statCourante->nombreDeCertifsGenereesEnPisicine++;
  }

  void CertificationPool::cert2lien(Certification* cert, int to, int j, bool majWoTb) {
    Lien lien = cert->link;
    uint32_t from = lien.first;
    Identity* certifieur = cert->emetteur;
    Identity* certifie = cert->receveur;
    if (certifie->estMembre) {
      Log2() << "LIEN de UID " << from << " -> " << to << " | WID " << certifieur->wotb_id << " -> " << certifie->wotb_id;
      if (majWoTb) {
        certifieur->wotb_node->addLinkTo(certifie->wotb_node);
      }
      // Ajoute dans la liste des membres
      liens[to].push_back(cert);
      // Retire de la liste des certificats potentiels
      certs[to].erase(certs[to].begin() + j);
      // On ne fera expirer que les liens qui auront la possibilité d'expirer durant la simu
      if (cert->dateOfIssuance + SIG_VALIDITY < NOMBRE_DE_BLOCKS_DE_SIMULATION) {
        expirationsDeLiens[cert->dateOfIssuance + SIG_VALIDITY].push_back(cert);
      }
      statCourante->nombreDeCertifsTransfereesEnToile++;
    }
  };

  void CertificationPool::faitExpirerLesLiens(uint32_t blocCourant) {

    /*********************
     * PASSAGE DU TEMPS
     *
     * Expiration des liens
     * -------------------
     *
     * Les liens existants dans la WoT expirent et s'auto-détruisent.
     */

    auto start = std::chrono::high_resolution_clock::now();
    vector<Certification*> liensQuiExpirent = expirationsDeLiens[blocCourant];
    for (int i = 0; i < liensQuiExpirent.size(); i++) {
      Certification* certExpirant = liensQuiExpirent[i];
      int to = certExpirant->link.second;
      for (int j = 0; j < liens[to].size(); j++) {
        Certification* certCourant = liens[to][j];
        bool memeDate = certCourant->dateOfIssuance == certExpirant->dateOfIssuance;
        bool memeEmetteur = certCourant->link.first == certExpirant->link.first;
        if (memeDate && memeEmetteur) {
          supprimeLien(liens[to][j], to, j);
          statCourante->nombreDeCertifsExpirees++;
          if (liens[to].size() < SIG_QTY) {
            certCourant->receveur->estMembre = false;
            certCourant->receveur->wotb_node->setEnabled(false);
            statCourante->nombreDeMembresExclusParManqueDeCertif++;
          }
        }
      }
    }
    statCourante->tempsExecutionSuppressionLiens = StatsDuTour::compteMicrosecondesDepuis(start);
  }

  void CertificationPool::supprimeLien(Certification* cert, int to, int j) {
    Log2() << "SUPPRIME Lien de UID " << cert->link.first << " -> " << cert->link.second << " | WID " << cert->emetteur->wotb_id << " -> " << cert->receveur->wotb_id;
    uint32_t from = cert->link.first;
    Identity* certifieur = cert->emetteur;
    Identity* certifie = cert->receveur;
    certifieur->wotb_node->removeLinkTo(certifie->wotb_node);
    // Retire de la liste des certificats de toile
    liens[to].erase(liens[to].begin() + j);
  };

  void CertificationPool::allPendingsToLinks() {
    // Ajoute les liens initiaux
    for (int to = 0; to < certs.size(); to++) {
      for (int j = certs[to].size() - 1; j >= 0; j--) {
        cert2lien(certs[to][j], to, j, true);
        statCourante->nombreDeTentativesDAjoutCertNouveauVenuSucces++;
      }
    }
  };

  bool CertificationPool::essaieIntegrerLien(Certification* cert, int to, int j) {
    uint32_t from = cert->link.first;
    Identity* certifieur = cert->emetteur;
    Identity* certifie = cert->receveur;
    if (certifieur->estMembre && certifie->estMembre) {
      Node* wotbCertifieur = certifieur->wotb_node;
      Node* wotbCertifie = certifie->wotb_node;
      bool echecParLadhesion = !(wotbCertifie != NULL && wotbCertifieur->isEnabled() && wotbCertifie->isEnabled());
      bool echecParLeStock = !echecParLadhesion && wotbCertifieur->getNbIssued() >= SIG_STOCK;
      if (!echecParLadhesion && !echecParLeStock) {
        if (wotbCertifieur->addLinkTo(wotbCertifie)) {
          cert2lien(cert, to, j, false);
          return true;
        } else {
          statCourante->nombreDeTentativesDAjoutCertInterneEchouees++;
          Log2() << "ECHEC de l'ajout du lien UID " << from << " -> " << to << " | WID " << certifieur->wotb_id << " -> " << certifie->wotb_id;
          Log2();
        }
      } else {
        statCourante->nombreDeTentativesDAjoutCertInterneEchouees++;
        if (echecParLadhesion) statCourante->nombreDeTentativesDAjoutCertInterneEchoueesParAdhesion++;
        if (echecParLeStock) statCourante->nombreDeTentativesDAjoutCertInterneEchoueesParStock++;
        Log2() << "ECHEC de l'ajout du lien UID " << from << " -> " << to << " | WID " << certifieur->wotb_id << " -> " << certifie->wotb_id;
        Log2();
      }
    }
    return false;
  };

  void CertificationPool::essaieIntegrerLiensInternes(IdentityPool *iPool) {
    auto start = std::chrono::high_resolution_clock::now();
    // Ajoute les liens internes (membre à membre)
    for (int to = 0; to < iPool->members.size(); to++) {
      for (int j = 0; j < certs[to].size(); j++) {
        statCourante->nombreDeTentativesDAjoutCertInterne++;
        if (essaieIntegrerLien(certs[to][j], to, j)) {
          statCourante->nombreDeTentativesDAjoutCertInterneSucces++;
        }
      }
    }
    statCourante->tempsExecutionIntegrationLiensInternes = StatsDuTour::compteMicrosecondesDepuis(start);
  }

  void CertificationPool::essaieIntegrerNouveauxVenus(WebOfTrust *wot, IdentityPool *iPool) {
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iPool->newcomers.size(); i++) {
      essaieIntegrerNouveauVenu(iPool->newcomers[i], wot, iPool);
    }
    statCourante->tempsExecutionIntegrationNouveauxVenus = StatsDuTour::compteMicrosecondesDepuis(start);
  }

  void CertificationPool::essaieIntegrerNouveauVenu(Identity *nouveau, WebOfTrust* wot, IdentityPool* iPool) {
    statCourante->nombreDeTentativesDAjoutMembre++;
    vector<Certification*> liensPotentiels = certs[nouveau->uid];
    vector<int> liensEffectifs;
    if (liensPotentiels.size() >= SIG_QTY) {
      // Passe la règle de quantités de signatures.
      // Ensuite, tester la distance :
      wot->addNode();
      uint32_t wotb_id = wot->getSize() - 1;
      for (int i = 0; i < liensPotentiels.size(); i++) {
        uint32_t from = liensPotentiels[i]->link.first;
        Identity* certifieur = liensPotentiels[i]->emetteur;
        Node* noeudCertifieur = certifieur->wotb_node;
        statCourante->nombreDeTentativesDAjoutCertNouveauVenu++;
        if (noeudCertifieur->getNbIssued() < SIG_STOCK && noeudCertifieur->addLinkTo(wotb_id)) {
          liensEffectifs.push_back(i);
        } else {
          statCourante->nombreDeTentativesDAjoutCertNouveauVenuEchouees++;
          statCourante->nombreDeTentativesDAjoutCertNouveauVenuEchoueesParStock++;
//            Log() << "ECHEC de l'ajout du lien UID " << from << " -> " << nouveau->uid << " | WID " << certifieur->wotb_id << " -> " << nouveau->wotb_id;
        }
      }
      uint32_t d_min = ceil(pow(wot->getSize(), 1 / STEPMAX));
      statCourante->nombreDeLiensEmisPourEtreSentry = d_min;
      statCourante->nombreDeSentries = wot->getSentries(d_min).nbNodes;
      bool echecSigQty = liensEffectifs.size() < SIG_QTY;
      bool echecDistance = !echecSigQty && wot->computeDistance(wotb_id, d_min, STEPMAX, X_PERCENT).isOutdistanced;
      if (echecSigQty || echecDistance) {

//        Log() << "ECHEC AJOUT EN TOILE";
//        wot->showTable();
        // Rollback: pop the last node (and its links)
        wot->removeNode();
        for (int j = liensEffectifs.size() - 1; j >= 0; j--) {
          statCourante->nombreDeTentativesDAjoutCertNouveauVenuEchouees++;
          if (echecSigQty) statCourante->nombreDeTentativesDAjoutCertNouveauVenuEchoueesParQteLiens++;
          if (echecDistance) statCourante->nombreDeTentativesDAjoutCertNouveauVenuEchoueesParDistance++;
        }
        if (echecSigQty) statCourante->nombreDeTentativesDAjoutMembreEchoueesParQteLiens++;
        if (echecDistance) statCourante->nombreDeTentativesDAjoutMembreEchoueesParDistance++;
        statCourante->nombreDeTentativesDAjoutMembreEchouees++;
      } else {
        nouveau->wotb_id = wotb_id;
        nouveau->estMembre = true;
        nouveau->aEteMembre = true;
        nouveau->wotb_node = wot->getNodeAt(wotb_id);
        iPool->newcomer2member(nouveau);
        for (int j = liensEffectifs.size() - 1; j >= 0; j--) {
          int position = liensEffectifs[j];
//            liensPotentiels[position]->receveur->
          cert2lien(liensPotentiels[position], nouveau->uid, position, false);
          statCourante->nombreDeTentativesDAjoutCertNouveauVenuSucces++;
        }
      }
    }
  };

  void CertificationPool::membreEmetUneCertifSiPossible(IdentityPool* iPool, Identity *emetteur, uint32_t blocCourant) {
    if (emetteur->sigPersoCible > 0) {
      uint32_t multipleDeSigPeriod = SIG_STOCK / emetteur->sigPersoCible;
      if (emetteur->derniereEmissionDeCertif + multipleDeSigPeriod * SIG_PERIOD <= blocCourant) {
        // Emet une certification aléatoirement
        uint32_t nbPossibilites = iPool->newcomers.size() + iPool->members.size();
        int nbEssais = 0;
        Identity* identiteCiblee = NULL;
        // Priorite aux liens bidirectionnels
//          Node* noeudEmetteur = wot->getNodeAt(emetteur->wotb_id);
//          for (int i = 0; identiteCiblee == NULL && i < noeudEmetteur->getNbLinks(); i++) {
//            WID indexNoeudCertifieurDeSoi = wot->getNodeIndex(noeudEmetteur->mCert[i]);
//            identiteCiblee = wotIdentities[indexNoeudCertifieurDeSoi];
//            if (existeDejaCertification(emetteur, identiteCiblee)) {
//              identiteCiblee = NULL;
//            }
//          }
        // Puis bascule éventuellement sur la certification d'un membre de façon aléatoire
        while (identiteCiblee == NULL && nbEssais < 100) {
          int cible = nombreAleatoireUniformeEntreXetY(0, nbPossibilites - 1);
          if (cible < iPool->newcomers.size()) {
            identiteCiblee = iPool->newcomers[cible];
          } else {
            identiteCiblee = iPool->members[cible - iPool->newcomers.size()];
          }
          bool luiMeme = emetteur->uid == identiteCiblee->uid;
          if (luiMeme) {
            identiteCiblee = NULL;
          }
          else if (existeDejaCertification(emetteur, identiteCiblee)) {
            identiteCiblee = NULL;
          }
          nbEssais++;
        };
        if (identiteCiblee != NULL) {
          createNew(emetteur, identiteCiblee, blocCourant);
        }
      }
    }
  };

  void CertificationPool::emetDeNouvellesCertifications(IdentityPool* iPool, uint32_t blocCourant) {

    /*********************
     *   CERTIFICATIONS
     *
     * Stratégie d'émission (fréquence)
     * --------------------------------
     *
     * Chaque *membre* émet 1 certification par (sigPeriod unités de temps * (sigStock / sigPerso))
     * Ex. : pour 1 membre à 50 certifications en moyenne pour un stock max à 150, et un sigPeriod = 1 semaine,
     * le membre émettra 1 certification toutes les 3 semaines (il étale son niveau personnel dans le temps).
     *
     * --> Il s'agit d'une stratégie linéaire. En réalité, on peut imaginer que chaque membre émet beaucoup de
     * certifications au début (relativement à sa moyenne personnelle cible) puis moins à la fin, suivant une courbe
     * logarithmique. Il faut donc tester plusieurs stratégies.
     *
     * Stratégie d'émission (cible)
     * ----------------------------
     *
     * On considère ici que le membre émet indifféremment à la toile existante ou aux nouveaux membres potentiels.
     * Toutefois, le nombre de nouveaux membres est borné à 5% maximum de la toile existante.
     *
     * --> Il s'agit d'une stratégie particulière, d'autres peuvent être imaginées.
     */

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iPool->members.size(); i++) {
      membreEmetUneCertifSiPossible(iPool, iPool->members[i], blocCourant);
    }
    statCourante->tempsExecutionMembreEmetUneCertifSiPossible = StatsDuTour::compteMicrosecondesDepuis(start);
  }

  int CertificationPool::nombreAleatoireUniformeEntreXetY(uint32_t x, uint32_t y) {
    std::uniform_int_distribution<> distr(x, y);
    return distr(eng);
  }

  bool CertificationPool::existeDejaCertification(Identity* emetteur, Identity* identiteCiblee) {
    // Test en piscine
    vector<Certification*> recuesEnPiscine = certs[identiteCiblee->uid];
    bool existeEnPiscine = false;
    uint32_t tailleDeLaPiscine = recuesEnPiscine.size();
    for (int i = 0; !existeEnPiscine && i < tailleDeLaPiscine; i++) {
      int fromUID = recuesEnPiscine[i]->link.first;
      if (fromUID == emetteur->uid) {
        existeEnPiscine = true;
      }
    }
    if (existeEnPiscine) {
      return true;
    }
    // Test en toile
    vector<Certification*> recuesEnToile = liens[identiteCiblee->uid];
    bool existeEnToile = false;
    uint32_t tailleDeLaToile = recuesEnToile.size();
    for (int i = 0; !existeEnToile && i < tailleDeLaToile; i++) {
      int fromUID = recuesEnToile[i]->link.first;
      if (fromUID == emetteur->uid) {
        existeEnToile = true;
      }
    }
    return existeEnToile;
  }
}