<?php
/**
 * @file
 * Class definition of FeedsNodeProcessor.
 */

/**
 * Creates file entities from feed items.
 */
class FeedsFileProcessor extends FeedsProcessor {
  protected $debug;

  /**
   * Define entity type.
   */
  public function entityType() {
    return 'file';
  }

  /**
   * Implements parent::entityInfo().
   */
  protected function entityInfo() {
    $info = parent::entityInfo();
    $info['label plural'] = t('Files');
    return $info;
  }

  /**
   * Create a new entity.
   *
   * @param $source
   *   The feeds source that spawns this entity.
   *
   * @return
   *   A new entity object.
   */
  protected function newEntity(FeedsSource $source) {
    $file = new stdClass();
    $file->type = $this->config['bundle'] ? $this->config['bundle'] : FILE_TYPE_NONE;
    $file->timestamp = REQUEST_TIME;
    $file->uid = $this->config['author'];
    $file->status = FILE_STATUS_PERMANENT;

    $this->debug($file, 'newEntity');

    return $file;
  }

  /**
   * Load an existing entity.
   *
   * @param $source
   *   The feeds source that spawns this entity.
   * @param $entity_id
   *   The unique id of the entity that should be loaded.
   *
   * @return
   *   A new entity object.
   *
   * @todo We should be able to batch load these, if we found all of the
   *   existing ids first.
   */
  protected function entityLoad(FeedsSource $source, $fid) {
    if ($this->config['update_existing'] == FEEDS_UPDATE_EXISTING) {
      $file = file_load($fid);
    }
    else {
      // We're replacing the existing file. Only load what's absolutely necessary.
      $file = db_query("SELECT fid, type, status, uri FROM {file_managed} WHERE fid = :fid", array(':fid' => $fid))->fetchObject();
      $file->uid = $this->config['author'];
    }

    $this->debug($file, 'entityLoad');

    return $file;
  }

  /**
   * Validate an entity.
   * @param $entity
   *   Entity to be saved.
   * @param FeedsSource $source
   *  (optional) The source to import from.
   *
   * @throws FeedsValidationException $e
   *   If validation fails.
   */
  protected function entityValidate($entity, FeedsSource $source = NULL) {
    if (empty($entity->uri)) {
      throw new FeedsValidationException(t('Missing file URI.'));
    }
  }

  /**
   * Access check for saving an enity.
   *
   * @param $entity
   *   Entity to be saved.
   *
   * @throws FeedsAccessException $e
   *   If the access check fails.
   */
  protected function entitySaveAccess($entity) {

    // The check will be skipped for anonymous files.
    if ($this->config['authorize'] && !empty($entity->uid)) {

      $author = user_load($entity->uid);

      // If the uid was mapped directly, rather than by email or username, it
      // could be invalid.
      if (!$author) {
        $message = 'The ID %uid does not represent a valid user account.';
        throw new FeedsAccessException(t($message, array('%uid' => $entity->uid)));
      }

      if (empty($entity->fid)) {
        $op = 'create';
        $access = file_entity_access($op, $entity->type, $author);
      }
      else {
        $op = 'update';
        $access = file_entity_access($op, $entity, $author);
      }

      if (!$access) {
        $message = 'The user account %name is not authorized to %op file type %type.';
        throw new FeedsAccessException(t($message, array('%name' => $author->name, '%op' => $op, '%type' => $entity->type)));
      }
    }
  }

  /**
   * Save an entity.
   *
   * @param $entity
   *   Entity to be saved.
   */
  public function entitySave($entity) {
    $this->debug($entity, 'entitySave');
    dpm($entity->uri);

    if (!file_is_local($entity)) {
      $file = FALSE;
      try {
        $file = $this->entityCopyLocal($entity);
      }
      catch (Exception $e) {
        // Ignore any errors and let the file API try and handle things.
      }

      if ($file) {
        $entity = $file;
      }
    }

    // TODO: What if it's local but not in the Backdrop path?
    // file_default_scheme() . ':/'
    if (is_a($entity, 'File')) {
      file_save($entity);
    }
    else {
      $message = 'Unable to save file @file';
      $replacements = array('@file' => $entity->filename);
      watchdog('feeds', $message, $replacements, WATCHDOG_ERROR);
    }
  }

  protected function entityCopyLocal($entity) {
    try {
      // Copy to temporary first.
      $source_uri = file_stream_wrapper_uri_normalize('temporary://' . basename($entity->uri));
      if (!@copy(@$entity->uri, $source_uri)) {
        throw new Exception('Unable to copy file ' . $entity->uri);
        return;
      }

      $entity->uri = $source_uri;
      $uri = file_build_uri($entity->filename);

      // Now to its new home.
      if (is_a($source_file, 'File')) {
        $file = file_move($entity, $uri, FILE_EXISTS_RENAME);
        return $file;
      }

      return FALSE;
    }
    catch (Exception $e) {
      // Do nothing on error. We will let it fail out on entitySave if it's going to fail.
    }

    return FALSE;
  }

  /**
   * Delete a series of entities.
   *
   * @param $entity_ids
   *   Array of unique identity ids to be deleted.
   */
  protected function entityDeleteMultiple($fids) {
    $files = file_load_multiple($fids);
    foreach ($files as $file) {
      file_delete($file);
    }
  }

  /**
   * Override parent::configDefaults().
   */
  public function configDefaults() {
    $entity_info = entity_get_info('file');
    $types = $entity_info['bundles'];
    $type = isset($types['image']) ? 'image' : FILE_TYPE_NONE;

    return array(
      'bundle' => $type,
      'author' => 0,
      'authorize' => TRUE,
      'debug' => array(),
    ) + parent::configDefaults();
  }

  /**
   * Override parent::configForm().
   */
  public function configForm(&$form_state) {
    $form = parent::configForm($form_state);

    // There is no body field on files.
    unset($form['input_format']);

    $author = user_load($this->config['author']);
    $form['author'] = array(
      '#type' => 'textfield',
      '#title' => t('Author'),
      '#description' => t('Select the author of the files to be created - leave empty to assign "anonymous".'),
      '#autocomplete_path' => 'user/autocomplete',
      '#default_value' => empty($author->name) ?  'anonymous' : check_plain($author->name),
    );
    $form['authorize'] = array(
      '#type' => 'checkbox',
      '#title' => t('Authorize'),
      '#description' => t('Check that the author has permission to create the file.'),
      '#default_value' => $this->config['authorize'],
    );

    $debug_options = array(
      'newEntity' => 'newEntity',
      'entityLoad' => 'entityLoad',
      'entitySave' => 'entitySave',
      'setTargetElement' => 'setTargetElement',
      'existingEntityId' => 'existingEntityId',
    );
    $form['debug_fieldset'] = array(
      '#type' => 'fieldset',
      '#title' => t('Debug'),
      '#collapsible' => TRUE,
      '#collapsed' => TRUE,
    );
    $form['debug_fieldset']['debug'] = array(
      '#type' => 'checkboxes',
      '#title' => t('Debug query'),
      '#options' => $debug_options,
      '#default_value' => isset($this->config['debug']) ? $this->config['debug'] : array(),
    );

    return $form;
  }

  /**
   * Override parent::configFormValidate().
   */
  public function configFormValidate(&$values) {
    if ($author = user_load_by_name($values['author'])) {
      $values['author'] = $author->uid;
    }
    else {
      $values['author'] = 0;
    }
  }

  /**
   * Return available mapping targets.
   */
  public function getMappingTargets() {
    $targets = parent::getMappingTargets();
    $targets['CAUTION: fid'] = array(
      'name' => t('File ID'),
      'description' => t('The fid of the file. NOTE: use this feature with care, file ids are usually assigned by Backdrop CMS.'),
      'optional_unique' => TRUE,
    );
    $targets['filename'] = array(
      'name' => t('Filename'),
      'description' => t('The filename of the file.'),
      'optional_unique' => TRUE,
    );
    $targets['timestamp'] = array(
      'name' => t('File created date'),
      'description' => t('The timestamp when the file was created.'),
    );
    $targets['uri'] = array(
      'name' => t('CAUTION: File URI'),
      'description' => t('The URI of the file. NOTE: use this feature with care, file URIs are usually assigned by Backdrop CMS.'),
      'optional_unique' => TRUE,
    );
    $targets['CAUTION: bundle'] = array(
      'name' => t('File Type'),
      'description' => t('The type/bundle of the file. NOTE: use this feature with care, file types are usually assigned by Backdrop CMS.'),
    );
    $targets['uid'] = array(
      'name' => t('User account ID (author)'),
      'description' => t('The user account UID of the file author.'),
    );
    $targets['user_name'] = array(
      'name' => t('Username (author)'),
      'description' => t('The username used to find the UID for the file author.'),
    );
    $targets['user_mail'] = array(
      'name' => t('User account email (author)'),
      'description' => t('The email address used to find the UID for the file author.'),
    );

    // Let other modules expose mapping targets.
    self::loadMappers();
    $entity_type = $this->entityType();
    $bundle = $this->config['bundle'] ? $this->config['bundle'] : FILE_TYPE_NONE;
    backdrop_alter('feeds_processor_targets', $targets, $entity_type, $bundle);

    return $targets;
  }

  /**
   * Override setTargetElement to operate on a target item that is a file.
   */
  public function setTargetElement(FeedsSource $source, $target_file, $target_element, $value) {
    $this->debug(array($source, $target_file, $target_element, $value), 'setTargetElement');

    switch ($target_element) {
      case 'uri':
        if (isset($value)) {
          // There will be aFID if the URI already exists.
          $query = db_select('file_managed', 'f')
            ->fields('f', array('fid'))
            ->condition('uri', $value)
            ->range(0, 1);
          $fid = $query->execute()->fetchField();

          if ($fid) {
            // Get full file object.
            $file = file_load($fid);
            $target_file->filename = $file->filename;
            $target_file->uri = $file->uri;
            $target_file->filemime = $file->filemime;
            $target_file->filesize = $file->filesize;
            $target_file->status = $file->status;
          }
        }
        else {
          $target_file->uri = $value;
        }
        break;
      case 'user_name':
        if ($user = user_load_by_name($value)) {
          $target_file->uid = $user->uid;
        }
        break;
      case 'user_mail':
        if ($user = user_load_by_mail($value)) {
          $target_file->uid = $user->uid;
        }
        break;
      default:
        parent::setTargetElement($source, $target_file, $target_element, $value);
        break;
    }
  }

  /**
   * Get nid of an existing feed item node if available.
   */
  protected function existingEntityId(FeedsSource $source, FeedsParserResult $result) {
    if ($fid = parent::existingEntityId($source, $result)) {
      return $fid;
    }

    // Iterate through all unique targets and test whether they do already
    // exist in the database.
    $unique_targets = $this->uniqueTargets($source, $result);
    $this->debug($unique_targets, 'existingEntityId');

    foreach ($unique_targets as $target => $value) {
      switch ($target) {
        case 'fid':
          $fid = db_query("SELECT fid FROM {file_managed} WHERE fid = :fid", array(':fid' => $value))->fetchField();
          break;

        case 'filename':
          $fid = db_query("SELECT fid FROM {file_managed} WHERE filename = :filename", array(':filename' => $value))->fetchField();
          break;

        case 'uri':
          $uri = FALSE;
          $value = trim($value);

          // Do we have a valid preformed URI?
          if (file_valid_uri($value)) {
            $uri = $value;
          }
          // Else, just try to use the site's default scheme
          else {
            $uri = file_default_scheme() . '://' . $value;
          }

          if ($uri) {
            $uri = file_stream_wrapper_uri_normalize($uri);
            $fid = db_query("SELECT fid FROM {file_managed} WHERE uri = :uri", array(':uri' => $uri))->fetchField();
          }
          break;
      }
      if ($fid) {
        // Return with the first fid found.
        return $fid;
      }
    }
    return 0;
  }

  protected function debug($item, $source) {
    if (!isset($this->debug)) {
      $debug = isset($this->config['debug']) ? $this->config['debug'] : array();
      $this->debug = array_keys(array_filter($debug));
    }

    if (in_array($source, $this->debug)) {
      $o = '<ul>';
      foreach ($item as $key => $val) {
        $o .= '<li>' . check_plain($key) . ' => ' . check_plain(var_export($val, TRUE)) . '</li>';
      }
      $o .= '</ul>';
      backdrop_set_message($source . ':' . $o);
    }
  }
}

